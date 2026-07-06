/*
*
* */
#define CATCH_CONFIG_MAIN

#include "catch.hpp"
#include "threads.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

std::string shell_quote(const std::string& value)
{
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

std::string test_home_without_ignore()
{
    static const std::string home =
        (std::filesystem::temp_directory_path() /
         ("mgrep_empty_home_" + std::to_string(getpid()))).string();
    return home;
}

std::string run_mgrep(
    const std::vector<std::string>& args,
    int* exit_code = nullptr,
    const std::string& home_dir = test_home_without_ignore(),
    bool force_no_color = true
)
{
    std::string command = "HOME=";
    command += shell_quote(home_dir);
    command += " ./mgrep";
    if (force_no_color) {
        command += " --no-color";
    }
    for (const auto& arg : args) {
        command.push_back(' ');
        command += shell_quote(arg);
    }
    command += " 2>&1";

    std::array<char, 4096> buffer{};
    std::string output;

    FILE* pipe = popen(command.c_str(), "r");
    REQUIRE(pipe != nullptr);

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }

    const int status = pclose(pipe);
    if (exit_code != nullptr) {
        *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
    }

    return output;
}

std::string run_mgrep_with_stdin(
    const std::string& input,
    const std::vector<std::string>& args,
    int* exit_code = nullptr,
    bool force_no_color = true
)
{
    static std::atomic<unsigned int> counter{0};
    std::ostringstream name;
    name << "mgrep_stdin_test_" << getpid() << "_" << counter++;
    const std::filesystem::path input_path = std::filesystem::temp_directory_path() / name.str();

    {
        std::ofstream out(input_path, std::ios::binary);
        REQUIRE(out);
        out << input;
    }

    std::string command = "HOME=";
    command += shell_quote(test_home_without_ignore());
    command += " ./mgrep";
    if (force_no_color) {
        command += " --no-color";
    }
    for (const auto& arg : args) {
        command.push_back(' ');
        command += shell_quote(arg);
    }
    command += " < ";
    command += shell_quote(input_path.string());
    command += " 2>&1";

    std::array<char, 4096> buffer{};
    std::string output;

    FILE* pipe = popen(command.c_str(), "r");
    if (pipe != nullptr) {
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            output += buffer.data();
        }
    }

    const int status = pipe != nullptr ? pclose(pipe) : -1;
    std::error_code ec;
    std::filesystem::remove(input_path, ec);

    REQUIRE(pipe != nullptr);

    if (exit_code != nullptr) {
        *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
    }

    return output;
}

void write_file(const std::filesystem::path& path, const std::string& contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out);
    out << contents;
}

std::string display_path(const std::filesystem::path& path)
{
    return path.parent_path().string() + "/\t" + path.filename().string();
}

struct CliFixture {
    std::filesystem::path root;

    CliFixture()
    {
        static std::atomic<unsigned int> counter{0};
        std::ostringstream name;
        name << "mgrep_cli_test_" << getpid() << "_" << counter++;
        root = std::filesystem::temp_directory_path() / name.str();

        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "src" / "nested");

        write_file(root / "src" / "one.txt",
                   "alpha\nneedle here\nomega\nneedle again\n");
        write_file(root / "src" / "regex_chars.txt",
                   "literal int main( should match\nregex chars a+b?[c]\n");
        write_file(root / "src" / "no_newline.txt",
                   "final needle without newline");
        write_file(root / "top.txt",
                   "needle at root\n");
        write_file(root / "src" / "two.md",
                   "nothing to see\n");
        write_file(root / "src" / "long_line.txt",
                   std::string(140 * 1024, 'x') + "needle at the end\n");
        write_file(root / "src" / "nested" / "three.cpp",
                   "first\nneedle nested\nlast\n");
        write_file(root / "skip.bin",
                   "needle skipped by extension\n");
        write_file(root / "no_extension",
                   "needle skipped because there is no extension\n");
        write_file(root / ".git" / "hidden.txt",
                   "needle skipped inside git metadata\n");
        write_file(root / "build" / "generated.txt",
                   "needle skipped inside build output\n");
        write_file(root / "binary.dat",
                   std::string("needle\0skipped as binary\n", 25));
    }

    ~CliFixture()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

} // namespace

TEST_CASE("ThreadPool Construction")
{
    ThreadPool tp;
    REQUIRE_FALSE(tp.m_workers.empty());
    REQUIRE(tp.m_workers.size() == tp.m_w_stats.size());

    for (size_t i = 0; i < tp.m_w_stats.size(); ++i) {
        REQUIRE(tp.m_w_stats[i].ID == i);
    }
}

TEST_CASE("Work Allocation")
{
    ThreadPool tp;
    std::atomic<unsigned int> completed{0};

    for (unsigned int i = 0; i < 128; ++i) {
        tp.push_task([&completed]() {
            completed.fetch_add(1, std::memory_order_relaxed);
        });
    }

    tp.wait_for_all();
    REQUIRE(completed.load(std::memory_order_relaxed) == 128);
    REQUIRE(tp.count_tasks_completed() == 128);
}

TEST_CASE("Fast recursive search prints matching file paths and respects default skips")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-r", "needle", fixture.root.string()});

    REQUIRE(output.find(display_path(fixture.root / "src" / "one.txt")) != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "src" / "nested" / "three.cpp")) != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "skip.bin")) == std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "no_extension")) == std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / ".git" / "hidden.txt")) == std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "build" / "generated.txt")) == std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "binary.dat")) == std::string::npos);
}

TEST_CASE("All-files mode includes files skipped by default")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-ra", "needle", fixture.root.string()});

    REQUIRE(output.find(display_path(fixture.root / "skip.bin")) != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "no_extension")) != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / ".git" / "hidden.txt")) != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "build" / "generated.txt")) != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "binary.dat")) != std::string::npos);
}

TEST_CASE("Files option lists searchable files without a pattern")
{
    CliFixture fixture;
    int exit_code = -1;

    const std::string output = run_mgrep({
        "--files",
        fixture.root.string()
    }, &exit_code);

    REQUIRE(exit_code == 0);
    REQUIRE(output.find((fixture.root / "top.txt").string() + "\n") != std::string::npos);
    REQUIRE(output.find((fixture.root / "src" / "one.txt").string() + "\n") != std::string::npos);
    REQUIRE(output.find((fixture.root / "src" / "nested" / "three.cpp").string() + "\n") != std::string::npos);
    REQUIRE(output.find((fixture.root / "skip.bin").string()) == std::string::npos);
    REQUIRE(output.find((fixture.root / "no_extension").string()) == std::string::npos);
    REQUIRE(output.find((fixture.root / ".git" / "hidden.txt").string()) == std::string::npos);
    REQUIRE(output.find((fixture.root / "build" / "generated.txt").string()) == std::string::npos);
    REQUIRE(output.find("needle here") == std::string::npos);
}

TEST_CASE("Files option composes with path filters and all-files mode")
{
    CliFixture fixture;
    int exit_code = -1;

    std::string output = run_mgrep({
        "--files",
        "--ext", "cpp",
        fixture.root.string()
    }, &exit_code);

    REQUIRE(exit_code == 0);
    REQUIRE(output.find((fixture.root / "src" / "nested" / "three.cpp").string() + "\n") != std::string::npos);
    REQUIRE(output.find((fixture.root / "src" / "one.txt").string()) == std::string::npos);

    output = run_mgrep({
        "--files",
        "-a",
        "--glob", "*.bin",
        fixture.root.string()
    }, &exit_code);

    REQUIRE(exit_code == 0);
    REQUIRE(output.find((fixture.root / "skip.bin").string() + "\n") != std::string::npos);
    REQUIRE(output.find((fixture.root / "binary.dat").string()) == std::string::npos);
}

TEST_CASE("Files option defaults to current directory")
{
    int exit_code = -1;

    const std::string output = run_mgrep({
        "--files",
        "-a",
        "--glob", "mgrep"
    }, &exit_code);

    REQUIRE(exit_code == 0);
    REQUIRE(output.find("./mgrep\n") != std::string::npos);
}

TEST_CASE("Type option can restrict recursive search to headers")
{
    CliFixture fixture;
    write_file(fixture.root / "include" / "defs.h", "needle header h\n");
    write_file(fixture.root / "include" / "defs.hpp", "needle header hpp\n");
    write_file(fixture.root / "src" / "impl.cpp", "needle source cpp\n");

    const std::string output = run_mgrep({
        "-rs",
        "--type", "header",
        "needle",
        fixture.root.string()
    });

    REQUIRE(output.find("needle header h\n") != std::string::npos);
    REQUIRE(output.find("needle header hpp\n") != std::string::npos);
    REQUIRE(output.find("needle source cpp\n") == std::string::npos);
    REQUIRE(output.find("needle nested\n") == std::string::npos);
}

TEST_CASE("Type option can restrict recursive search to source files")
{
    CliFixture fixture;
    write_file(fixture.root / "include" / "defs.h", "needle header h\n");
    write_file(fixture.root / "src" / "impl.cpp", "needle source cpp\n");

    const std::string output = run_mgrep({
        "-rs",
        "--type", "source",
        "needle",
        fixture.root.string()
    });

    REQUIRE(output.find("needle source cpp\n") != std::string::npos);
    REQUIRE(output.find("needle nested\n") != std::string::npos);
    REQUIRE(output.find("needle header h\n") == std::string::npos);
}

TEST_CASE("Ext option restricts recursive search to listed extensions")
{
    CliFixture fixture;
    write_file(fixture.root / "include" / "defs.h", "needle header h\n");
    write_file(fixture.root / "include" / "defs.HPP", "needle uppercase hpp\n");
    write_file(fixture.root / "src" / "impl.cpp", "needle source cpp\n");

    const std::string output = run_mgrep({
        "-rs",
        "--ext", "h,hpp",
        "needle",
        fixture.root.string()
    });

    REQUIRE(output.find("needle header h\n") != std::string::npos);
    REQUIRE(output.find("needle uppercase hpp\n") != std::string::npos);
    REQUIRE(output.find("needle source cpp\n") == std::string::npos);
}

TEST_CASE("Ext option filters explicit file paths")
{
    CliFixture fixture;
    const std::filesystem::path header = fixture.root / "defs.h";
    const std::filesystem::path source = fixture.root / "impl.cpp";
    write_file(header, "needle explicit header\n");
    write_file(source, "needle explicit source\n");

    const std::string output = run_mgrep({
        "-s",
        "--ext", "h",
        "needle",
        header.string(),
        source.string()
    });

    REQUIRE(output.find("needle explicit header\n") != std::string::npos);
    REQUIRE(output.find("needle explicit source\n") == std::string::npos);
}

TEST_CASE("Glob option filters recursive search by basename")
{
    CliFixture fixture;
    write_file(fixture.root / "include" / "defs.h", "needle header h\n");
    write_file(fixture.root / "include" / "defs.hpp", "needle header hpp\n");

    const std::string output = run_mgrep({
        "-rs",
        "--glob", "*.h",
        "needle",
        fixture.root.string()
    });

    REQUIRE(output.find("needle header h\n") != std::string::npos);
    REQUIRE(output.find("needle header hpp\n") == std::string::npos);
    REQUIRE(output.find("needle nested\n") == std::string::npos);
}

TEST_CASE("Glob option can match path suffixes with directories")
{
    CliFixture fixture;
    write_file(fixture.root / "include" / "defs.h", "needle include header\n");
    write_file(fixture.root / "src" / "defs.h", "needle src header\n");

    const std::string output = run_mgrep({
        "-rs",
        "--glob", "include/*.h",
        "needle",
        fixture.root.string()
    });

    REQUIRE(output.find("needle include header\n") != std::string::npos);
    REQUIRE(output.find("needle src header\n") == std::string::npos);
}

TEST_CASE("Glob option supports recursive double-star directories")
{
    CliFixture fixture;
    write_file(fixture.root / "include" / "defs.hpp", "needle shallow hpp\n");
    write_file(fixture.root / "include" / "detail" / "defs.hpp", "needle nested hpp\n");
    write_file(fixture.root / "src" / "defs.hpp", "needle src hpp\n");

    const std::string output = run_mgrep({
        "-rs",
        "--glob", "include/**/*.hpp",
        "needle",
        fixture.root.string()
    });

    REQUIRE(output.find("needle shallow hpp\n") != std::string::npos);
    REQUIRE(output.find("needle nested hpp\n") != std::string::npos);
    REQUIRE(output.find("needle src hpp\n") == std::string::npos);
}

TEST_CASE("Exclude-glob removes matching paths")
{
    CliFixture fixture;
    write_file(fixture.root / "src" / "defs.cpp", "needle production source\n");
    write_file(fixture.root / "src" / "defs_test.cpp", "needle test source\n");

    const std::string output = run_mgrep({
        "-rs",
        "--exclude-glob", "*test*",
        "needle",
        fixture.root.string()
    });

    REQUIRE(output.find("needle production source\n") != std::string::npos);
    REQUIRE(output.find("needle test source\n") == std::string::npos);
}

TEST_CASE("Exclude-glob prunes matching directories")
{
    CliFixture fixture;
    write_file(fixture.root / "src" / "defs.cpp", "needle production source\n");
    write_file(fixture.root / "vendor" / "defs.cpp", "needle vendor source\n");

    const std::string output = run_mgrep({
        "-rs",
        "--exclude-glob", "vendor",
        "needle",
        fixture.root.string()
    });

    REQUIRE(output.find("needle production source\n") != std::string::npos);
    REQUIRE(output.find("needle vendor source\n") == std::string::npos);
}

TEST_CASE("Exclude-glob trailing slash prunes matching directories")
{
    CliFixture fixture;
    write_file(fixture.root / "src" / "defs.cpp", "needle production source\n");
    write_file(fixture.root / "vendor" / "defs.cpp", "needle vendor source\n");

    const std::string output = run_mgrep({
        "-rs",
        "--exclude-glob", "vendor/",
        "needle",
        fixture.root.string()
    });

    REQUIRE(output.find("needle production source\n") != std::string::npos);
    REQUIRE(output.find("needle vendor source\n") == std::string::npos);
}

TEST_CASE("Glob and ext filters compose")
{
    CliFixture fixture;
    write_file(fixture.root / "include" / "defs.h", "needle include header\n");
    write_file(fixture.root / "include" / "defs.cpp", "needle include source\n");
    write_file(fixture.root / "src" / "defs.h", "needle src header\n");

    const std::string output = run_mgrep({
        "-rs",
        "--glob", "include/*",
        "--ext", "h",
        "needle",
        fixture.root.string()
    });

    REQUIRE(output.find("needle include header\n") != std::string::npos);
    REQUIRE(output.find("needle include source\n") == std::string::npos);
    REQUIRE(output.find("needle src header\n") == std::string::npos);
}

TEST_CASE("Fast file search skips binary-looking files with late NUL bytes")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path path = fixture.root / "late-nul.txt";
    write_file(path, "needle before binary marker\n" + std::string(140 * 1024, 'x') + '\0');

    const std::string output = run_mgrep({"needle", path.string()}, &exit_code);

    REQUIRE(exit_code == 1);
    REQUIRE(output.empty());
}

TEST_CASE("Huge source file output is not emitted before late NUL detection")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path path = fixture.root / "huge-source-late-nul.txt";
    write_file(
        path,
        std::string(2 * 1024 * 1024, 'a') + "needle\n" +
        std::string(128 * 1024, 'x') + '\0'
    );

    const std::string output = run_mgrep({"-s", "needle", path.string()}, &exit_code);

    REQUIRE(exit_code == 1);
    REQUIRE(output.empty());
}

TEST_CASE("Default traversal skips binary extensions case-insensitively")
{
    CliFixture fixture;

    write_file(fixture.root / "upper.PNG", "needle uppercase png\n");
    write_file(fixture.root / "upper.ZIP", "needle uppercase zip\n");

    const std::string default_output = run_mgrep({"-r", "needle", fixture.root.string()});
    REQUIRE(default_output.find(display_path(fixture.root / "upper.PNG")) == std::string::npos);
    REQUIRE(default_output.find(display_path(fixture.root / "upper.ZIP")) == std::string::npos);

    const std::string all_files_output = run_mgrep({"-ra", "needle", fixture.root.string()});
    REQUIRE(all_files_output.find(display_path(fixture.root / "upper.PNG")) != std::string::npos);
    REQUIRE(all_files_output.find(display_path(fixture.root / "upper.ZIP")) != std::string::npos);
}

TEST_CASE("Explicit file paths bypass default traversal skips")
{
    CliFixture fixture;
    int exit_code = -1;

    const std::string output = run_mgrep({
        "needle",
        (fixture.root / "skip.bin").string(),
        (fixture.root / "no_extension").string()
    }, &exit_code);

    REQUIRE(exit_code == 0);
    REQUIRE(output.find(display_path(fixture.root / "skip.bin")) != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "no_extension")) != std::string::npos);
}

TEST_CASE("Explicit file path output preserves argument order after batching threshold")
{
    CliFixture fixture;
    int exit_code = -1;
    std::vector<std::string> args = {"-s", "needle"};

    for (int i = 0; i < 96; ++i) {
        const std::filesystem::path path =
            fixture.root / ("ordered_" + std::to_string(i) + ".txt");
        const std::string contents =
            i < 32 ? std::string(256 * 1024, 'x') + "needle " + std::to_string(i) + "\n"
                   : "needle " + std::to_string(i) + "\n";
        write_file(path, contents);
        args.push_back(path.string());
    }

    const std::string output = run_mgrep(args, &exit_code);
    size_t previous_pos = 0;

    REQUIRE(exit_code == 0);
    for (int i = 0; i < 96; ++i) {
        const std::string needle = "needle " + std::to_string(i) + "\n";
        const size_t pos = output.find(needle, previous_pos);
        REQUIRE(pos != std::string::npos);
        previous_pos = pos + needle.size();
    }
}

TEST_CASE("Home ignore file skips exact files suffixes and directories")
{
    CliFixture fixture;
    const std::filesystem::path fake_home = fixture.root / "home";

    write_file(fake_home / ".ignore",
               "ignored_dir/\nignored.txt\n*.generated\n");
    write_file(fixture.root / "visible.txt",
               "needle visible\n");
    write_file(fixture.root / "ignored.txt",
               "needle ignored exact file\n");
    write_file(fixture.root / "report.generated",
               "needle ignored suffix file\n");
    write_file(fixture.root / "ignored_dir" / "hit.txt",
               "needle ignored directory\n");

    const std::string output = run_mgrep(
        {"-r", "needle", fixture.root.string()},
        nullptr,
        fake_home.string()
    );

    REQUIRE(output.find(display_path(fixture.root / "visible.txt")) != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "ignored.txt")) == std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "report.generated")) == std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "ignored_dir" / "hit.txt")) == std::string::npos);
}

TEST_CASE("Home ignore suffix rules skip matching directories")
{
    CliFixture fixture;
    const std::filesystem::path fake_home = fixture.root / "home";

    write_file(fake_home / ".ignore",
               "*-venv/\n*_venv/\n");
    write_file(fixture.root / "testing-venv" / "hit.txt",
               "needle ignored suffix directory\n");
    write_file(fixture.root / "testing_venv" / "hit.txt",
               "needle ignored underscore suffix directory\n");
    write_file(fixture.root / "visible-env" / "hit.txt",
               "needle visible suffix miss\n");

    const std::string output = run_mgrep(
        {"-r", "needle", fixture.root.string()},
        nullptr,
        fake_home.string()
    );

    REQUIRE(output.find(display_path(fixture.root / "testing-venv" / "hit.txt")) == std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "testing_venv" / "hit.txt")) == std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "visible-env" / "hit.txt")) != std::string::npos);
}

TEST_CASE("Home ignore directory-only suffix rule does not skip matching files")
{
    CliFixture fixture;
    const std::filesystem::path fake_home = fixture.root / "home";

    write_file(fake_home / ".ignore", "*.generated/\n");
    write_file(fixture.root / "report.generated",
               "needle file should remain visible\n");
    write_file(fixture.root / "visible.txt",
               "needle visible\n");

    const std::string output = run_mgrep(
        {"-r", "needle", fixture.root.string()},
        nullptr,
        fake_home.string()
    );

    REQUIRE(output.find(display_path(fixture.root / "report.generated")) != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "visible.txt")) != std::string::npos);
}

TEST_CASE("Home ignore directory-only suffix rule skips matching directories")
{
    CliFixture fixture;
    const std::filesystem::path fake_home = fixture.root / "home";

    write_file(fake_home / ".ignore", "*.generated/\n");
    write_file(fixture.root / "report.generated" / "hit.txt",
               "needle directory should be ignored\n");
    write_file(fixture.root / "visible.txt",
               "needle visible\n");

    const std::string output = run_mgrep(
        {"-r", "needle", fixture.root.string()},
        nullptr,
        fake_home.string()
    );

    REQUIRE(output.find(display_path(fixture.root / "report.generated" / "hit.txt")) == std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "visible.txt")) != std::string::npos);
}

TEST_CASE("All-files mode bypasses home ignore file")
{
    CliFixture fixture;
    const std::filesystem::path fake_home = fixture.root / "home";

    write_file(fake_home / ".ignore",
               "ignored_dir/\nignored.txt\n*.generated\n");
    write_file(fixture.root / "ignored.txt",
               "needle ignored exact file\n");
    write_file(fixture.root / "report.generated",
               "needle ignored suffix file\n");
    write_file(fixture.root / "ignored_dir" / "hit.txt",
               "needle ignored directory\n");

    const std::string output = run_mgrep(
        {"-ra", "needle", fixture.root.string()},
        nullptr,
        fake_home.string()
    );

    REQUIRE(output.find(display_path(fixture.root / "ignored.txt")) != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "report.generated")) != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "ignored_dir" / "hit.txt")) != std::string::npos);
}

TEST_CASE("Line and source mode reports line numbers and matched source")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-rls", "needle here", fixture.root.string()});

    REQUIRE(output.find(display_path(fixture.root / "src" / "one.txt")) != std::string::npos);
    REQUIRE(output.find("\n2:\tneedle here") != std::string::npos);
}

TEST_CASE("Line and source mode separates matches with a blank line")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-rlsn", "needle", fixture.root.string()});

    REQUIRE(output.find("2:\tneedle here\n\n") != std::string::npos);
}

TEST_CASE("Heading mode groups source matches by file")
{
    CliFixture fixture;

    const std::string output = run_mgrep({
        "-rs",
        "--heading",
        "needle",
        (fixture.root / "src" / "one.txt").string()
    });

    const std::string expected =
        display_path(fixture.root / "src" / "one.txt") + "\n"
        "needle here\n"
        "needle again\n";

    REQUIRE(output == expected);
}

TEST_CASE("Heading mode prints line numbers under the file heading")
{
    CliFixture fixture;

    const std::string output = run_mgrep({
        "-rls",
        "--heading",
        "needle",
        (fixture.root / "src" / "one.txt").string()
    });

    const std::string expected =
        display_path(fixture.root / "src" / "one.txt") + "\n"
        "2:\tneedle here\n"
        "4:\tneedle again\n";

    REQUIRE(output == expected);
}

TEST_CASE("Heading mode groups context output under the file heading")
{
    CliFixture fixture;

    const std::string output = run_mgrep({
        "-s",
        "--heading",
        "-B", "1",
        "-A", "1",
        "needle here",
        (fixture.root / "src" / "one.txt").string()
    });

    const std::string expected =
        display_path(fixture.root / "src" / "one.txt") + "\n"
        "alpha\n"
        "needle here\n"
        "omega\n";

    REQUIRE(output == expected);
}

TEST_CASE("Heading mode does not change path-only output")
{
    CliFixture fixture;

    const std::string output = run_mgrep({
        "--heading",
        "needle here",
        (fixture.root / "src" / "one.txt").string()
    });

    REQUIRE(output == display_path(fixture.root / "src" / "one.txt") + "\n");
}

TEST_CASE("Heading mode groups huge source matches by file")
{
    CliFixture fixture;
    const std::filesystem::path path = fixture.root / "huge-heading.txt";
    const std::string first_line = std::string(1024 * 1024, 'a') + " needle one\n";
    const std::string second_line = std::string(1024 * 1024, 'b') + " needle two\n";
    write_file(path, first_line + second_line);

    const std::string output = run_mgrep({
        "-as",
        "--heading",
        "needle",
        path.string()
    });

    const std::string shown_path = display_path(path);
    REQUIRE(output.rfind(shown_path + "\n", 0) == 0);
    REQUIRE(output.find(shown_path, shown_path.size() + 1) == std::string::npos);
    REQUIRE(output.find(" needle one\n") != std::string::npos);
    REQUIRE(output.find(" needle two\n") != std::string::npos);
}

TEST_CASE("Ignore-case search matches ASCII case variants in files")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path path = fixture.root / "case.txt";
    write_file(path, "Needle mixed case\n");

    std::string output = run_mgrep({"needle", path.string()}, &exit_code);
    REQUIRE(exit_code == 1);
    REQUIRE(output.empty());

    output = run_mgrep({"-i", "needle", path.string()}, &exit_code);
    REQUIRE(exit_code == 0);
    REQUIRE(output == display_path(path) + "\n");
}

TEST_CASE("Piped stdin is searched when no path is supplied")
{
    const std::string output = run_mgrep_with_stdin(
        "alpha\nneedle from stdin\nomega\n",
        {"needle"}
    );

    REQUIRE(output == "needle from stdin\n");
}

TEST_CASE("Piped stdin exits with no-match status when no match is found")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "alpha\nomega\n",
        {"needle"},
        &exit_code
    );

    REQUIRE(exit_code == 1);
    REQUIRE(output.empty());
}

TEST_CASE("Dash path explicitly searches piped stdin")
{
    const std::string output = run_mgrep_with_stdin(
        "alpha\nneedle from dash\nomega\n",
        {"needle", "-"}
    );

    REQUIRE(output == "needle from dash\n");
}

TEST_CASE("Implicit and explicit stdin produce the same plain output")
{
    const std::string input = "alpha\nneedle from stdin\nomega\n";

    const std::string implicit_output = run_mgrep_with_stdin(input, {"needle"});
    const std::string explicit_output = run_mgrep_with_stdin(input, {"needle", "-"});

    REQUIRE(implicit_output == explicit_output);
}

TEST_CASE("Piped stdin supports short count option")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "needle one\nno match\nneedle two needle two\n",
        {"-c", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output == "2\n");
}

TEST_CASE("Piped stdin supports line numbers and color highlighting")
{
    const std::string output = run_mgrep_with_stdin(
        "alpha\nneedle from stdin\nomega\n",
        {"-pl", "needle"}
    );

    REQUIRE(output == "\033[38;5;37m2:\033[0m\t\033[38;5;81mneedle\033[0m from stdin\n");
}

TEST_CASE("Piped stdin supports source option without changing plain source output")
{
    const std::string output = run_mgrep_with_stdin(
        "alpha\nneedle from stdin\nomega\n",
        {"-s", "needle"}
    );

    REQUIRE(output == "needle from stdin\n");
}

TEST_CASE("Ignore-case stdin search preserves original source text")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "alpha\nNeedle from stdin\nomega\n",
        {"-is", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output == "Needle from stdin\n");
}

TEST_CASE("Ignore-case count mode counts ASCII case variants")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "Needle one\nno match\nneedle two\nNEEDLE three\n",
        {"-ic", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output == "3\n");
}

TEST_CASE("Ignore-case only-matching output preserves matched casing")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "Needle needle NEEDLE\n",
        {"-io", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output == "Needle\nneedle\nNEEDLE\n");
}

TEST_CASE("Ignore-case invert match excludes ASCII case variants")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "Needle\nother\nNEEDLE\n",
        {"-iv", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output == "other\n");
}

TEST_CASE("Binary-looking stdin is skipped by default")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        std::string("needle before nul\0needle after nul\n", 35),
        {"needle"},
        &exit_code
    );

    REQUIRE(exit_code == 1);
    REQUIRE(output.empty());
}

TEST_CASE("All-files mode searches binary-looking stdin")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        std::string("needle before nul\0needle after nul\n", 35),
        {"-qa", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output.empty());
}

TEST_CASE("Quiet mode treats binary-looking stdin as no match by default")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        std::string("needle before nul\0needle after nul\n", 35),
        {"-q", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 1);
    REQUIRE(output.empty());
}

TEST_CASE("Quiet stdin keeps scanning after early match to detect late NUL")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "needle\n" + std::string(256 * 1024, 'x') + '\0',
        {"-q", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 1);
    REQUIRE(output.empty());
}

TEST_CASE("Huge stdin source output is not flushed before late NUL detection")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        std::string(2 * 1024 * 1024, 'a') + "needle\n" +
        std::string(128 * 1024, 'x') + '\0',
        {"-s", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 1);
    REQUIRE(output.empty());
}

TEST_CASE("Empty pattern matches every stdin line")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "alpha\nbeta\n",
        {""},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output == "alpha\nbeta\n");
}

TEST_CASE("Piped stdin supports before and after context")
{
    const std::string output = run_mgrep_with_stdin(
        "before\nneedle from stdin\nafter\n",
        {"-B", "1", "-A", "1", "needle"}
    );

    REQUIRE(output == "before\nneedle from stdin\nafter\n");
}

TEST_CASE("Piped stdin quiet mode returns no-match status without output")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "alpha\nomega\n",
        {"-q", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 1);
    REQUIRE(output.empty());
}

TEST_CASE("Explicit dash can be mixed with file paths")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "needle from stdin\n",
        {"needle", (fixture.root / "top.txt").string(), "-"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output.find("needle from stdin\n") != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "top.txt")) != std::string::npos);
}

TEST_CASE("Explicit dash preserves argument output order")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "needle from stdin\n",
        {"-s", "needle", (fixture.root / "top.txt").string(), "-"},
        &exit_code
    );

    const size_t file_pos = output.find("needle at root\n");
    const size_t stdin_pos = output.find("needle from stdin\n");

    REQUIRE(exit_code == 0);
    REQUIRE(file_pos != std::string::npos);
    REQUIRE(stdin_pos != std::string::npos);
    REQUIRE(file_pos < stdin_pos);
}

TEST_CASE("Explicit dash mixed with files returns no-match status when nothing matches")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "stdin miss\n",
        {"not-present", (fixture.root / "src" / "two.md").string(), "-"},
        &exit_code
    );

    REQUIRE(exit_code == 1);
    REQUIRE(output.empty());
}

TEST_CASE("Explicit dash mixed with files supports count mode")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "needle from stdin\nneedle again\n",
        {"-c", "needle", (fixture.root / "top.txt").string(), "-"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output.find(display_path(fixture.root / "top.txt") + "\t1\n") != std::string::npos);
    REQUIRE(output.find("2\n") != std::string::npos);
}

TEST_CASE("Explicit dash mixed with files supports quiet mode")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "needle from stdin\n",
        {"-q", "needle", (fixture.root / "src" / "two.md").string(), "-"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output.empty());
}

TEST_CASE("Explicit dash supports only-matching mode")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "needle needle\n",
        {"-o", "needle", "-"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output == "needle\nneedle\n");
}

TEST_CASE("Explicit dash mixed with files supports context output")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "before\nneedle from stdin\nafter\n",
        {"-B", "1", "-A", "1", "needle", (fixture.root / "src" / "two.md").string(), "-"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output.find("before\nneedle from stdin\nafter\n") != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "src" / "two.md")) == std::string::npos);
}

TEST_CASE("Explicit dash mixed with files supports colors and line numbers")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "alpha\nneedle from stdin\n",
        {"-pl", "needle", (fixture.root / "src" / "two.md").string(), "-"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output.find("\033[38;5;37m2:\033[0m\t\033[38;5;81mneedle\033[0m from stdin\n") != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "src" / "two.md")) == std::string::npos);
}

TEST_CASE("Count mode reports matching line counts for files")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-rc", "needle", fixture.root.string()});

    REQUIRE(output.find(display_path(fixture.root / "src" / "one.txt") + "\t2\n") != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "src" / "nested" / "three.cpp") + "\t1\n") != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "src" / "two.md")) == std::string::npos);
}

TEST_CASE("Long count option reports stdin matching line count")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "needle one\nno match\nneedle two needle two\n",
        {"--count", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output == "2\n");
}

TEST_CASE("Count mode suppresses zero-count stdin output")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "alpha\nomega\n",
        {"-c", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 1);
    REQUIRE(output.empty());
}

TEST_CASE("Count mode exits with no-match status when count is zero")
{
    CliFixture fixture;
    int exit_code = -1;

    const std::string output = run_mgrep({"-rc", "not-present", fixture.root.string()}, &exit_code);

    REQUIRE(exit_code == 1);
    REQUIRE(output.empty());
}

TEST_CASE("Quiet mode suppresses file output and returns match status")
{
    CliFixture fixture;
    int exit_code = -1;

    const std::string output = run_mgrep({"-rq", "needle", fixture.root.string()}, &exit_code);

    REQUIRE(exit_code == 0);
    REQUIRE(output.empty());
}

TEST_CASE("Quiet mode returns no-match status without output")
{
    CliFixture fixture;
    int exit_code = -1;

    const std::string output = run_mgrep({"-rq", "not-present", fixture.root.string()}, &exit_code);

    REQUIRE(exit_code == 1);
    REQUIRE(output.empty());
}

TEST_CASE("Quiet mode stops before later missing path after a match")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path missing_path = fixture.root / "missing-after-match.txt";

    const std::string output = run_mgrep({
        "-q",
        "needle",
        (fixture.root / "top.txt").string(),
        missing_path.string()
    }, &exit_code);

    REQUIRE(exit_code == 0);
    REQUIRE(output.empty());
}

TEST_CASE("Quiet mode reports later missing path when no earlier match exists")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path missing_path = fixture.root / "missing-after-miss.txt";

    const std::string output = run_mgrep({
        "-q",
        "needle",
        (fixture.root / "src" / "two.md").string(),
        missing_path.string()
    }, &exit_code);

    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: path does not exist: " + missing_path.string()) != std::string::npos);
}

TEST_CASE("Long quiet option suppresses stdin output and returns match status")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "alpha\nneedle from stdin\nomega\n",
        {"--quiet", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output.empty());
}

TEST_CASE("Quiet mode overrides count output")
{
    CliFixture fixture;
    int exit_code = -1;

    const std::string output = run_mgrep({"-rcq", "needle", fixture.root.string()}, &exit_code);

    REQUIRE(exit_code == 0);
    REQUIRE(output.empty());
}

TEST_CASE("Quiet file search honors max-lines limit")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path path = fixture.root / "quiet-max-lines.txt";
    write_file(path, "first miss\nneedle after limit\n");

    const std::string output = run_mgrep({
        "-q",
        "-m", "1",
        "needle",
        path.string()
    }, &exit_code);

    REQUIRE(exit_code == 1);
    REQUIRE(output.empty());
}

TEST_CASE("Quiet stdin search honors max-lines limit")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "first miss\nneedle after limit\n",
        {"-q", "-m", "1", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 1);
    REQUIRE(output.empty());
}

TEST_CASE("Files-from searches newline-delimited explicit file paths")
{
    CliFixture fixture;
    const std::filesystem::path list_path = fixture.root / "files.txt";
    write_file(
        list_path,
        (fixture.root / "skip.bin").string() + "\n" +
        (fixture.root / "src" / "two.md").string() + "\n"
    );

    const std::string output = run_mgrep({
        "--files-from", list_path.string(),
        "needle"
    });

    REQUIRE(output.find(display_path(fixture.root / "skip.bin")) != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "src" / "two.md")) == std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "src" / "one.txt")) == std::string::npos);
}

TEST_CASE("Files-from accepts CRLF-delimited file paths")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path list_path = fixture.root / "crlf-files.txt";
    write_file(
        list_path,
        (fixture.root / "src" / "one.txt").string() + "\r\n"
    );

    const std::string output = run_mgrep({
        "--files-from", list_path.string(),
        "needle here"
    }, &exit_code);

    REQUIRE(exit_code == 0);
    REQUIRE(output.find(display_path(fixture.root / "src" / "one.txt")) != std::string::npos);
}

TEST_CASE("Files-from output stays before later directory traversal output")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path list_path = fixture.root / "ordered-files-list.txt";
    const std::filesystem::path dir_path = fixture.root / "ordered-dir";
    std::string list_contents;

    for (int i = 0; i < 32; ++i) {
        const std::filesystem::path path =
            fixture.root / ("listed_ordered_" + std::to_string(i) + ".txt");
        write_file(path, std::string(256 * 1024, 'x') + "needle listed " + std::to_string(i) + "\n");
        list_contents += path.string() + "\n";
    }

    for (int i = 0; i < 96; ++i) {
        write_file(
            dir_path / ("dir_ordered_" + std::to_string(i) + ".txt"),
            "needle directory " + std::to_string(i) + "\n"
        );
    }

    write_file(list_path, list_contents);

    const std::string output = run_mgrep({
        "-s",
        "--files-from", list_path.string(),
        "needle",
        dir_path.string()
    }, &exit_code);

    const size_t listed_pos = output.find("needle listed 0\n");
    const size_t dir_pos = output.find("needle directory ");

    REQUIRE(exit_code == 0);
    REQUIRE(listed_pos != std::string::npos);
    REQUIRE(dir_pos != std::string::npos);
    REQUIRE(listed_pos < dir_pos);
}

TEST_CASE("Explicit path output stays before later files-from output")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path explicit_path = fixture.root / "explicit-before-list.txt";
    const std::filesystem::path listed_path = fixture.root / "listed-after-explicit.txt";
    const std::filesystem::path list_path = fixture.root / "later-files-list.txt";

    write_file(explicit_path, "needle explicit first\n");
    write_file(listed_path, "needle listed second\n");
    write_file(list_path, listed_path.string() + "\n");

    const std::string output = run_mgrep({
        "-s",
        "needle",
        explicit_path.string(),
        "--files-from", list_path.string()
    }, &exit_code);

    const size_t explicit_pos = output.find("needle explicit first\n");
    const size_t listed_pos = output.find("needle listed second\n");

    REQUIRE(exit_code == 0);
    REQUIRE(explicit_pos != std::string::npos);
    REQUIRE(listed_pos != std::string::npos);
    REQUIRE(explicit_pos < listed_pos);
}

TEST_CASE("Mixed files-from operands preserve option order")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path nul_list = fixture.root / "ordered-files-list.nul";
    const std::filesystem::path newline_list = fixture.root / "ordered-files-list.txt";

    write_file(fixture.root / "listed-from-nul.txt", "needle from nul list\n");
    write_file(fixture.root / "listed-from-newline.txt", "needle from newline list\n");
    write_file(nul_list, (fixture.root / "listed-from-nul.txt").string() + '\0');
    write_file(newline_list, (fixture.root / "listed-from-newline.txt").string() + "\n");

    const std::string output = run_mgrep({
        "-s",
        "--files-from0", nul_list.string(),
        "--files-from", newline_list.string(),
        "needle"
    }, &exit_code);

    const size_t nul_pos = output.find("needle from nul list\n");
    const size_t newline_pos = output.find("needle from newline list\n");

    REQUIRE(exit_code == 0);
    REQUIRE(nul_pos != std::string::npos);
    REQUIRE(newline_pos != std::string::npos);
    REQUIRE(nul_pos < newline_pos);
}

TEST_CASE("Files-from0 searches NUL-delimited explicit file paths from stdin")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::string input =
        (fixture.root / "src" / "one.txt").string() + '\0' +
        (fixture.root / "src" / "nested" / "three.cpp").string() + '\0';

    const std::string output = run_mgrep_with_stdin(
        input,
        {"--files-from0", "-", "needle nested"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output.find(display_path(fixture.root / "src" / "nested" / "three.cpp")) != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "src" / "one.txt")) == std::string::npos);
}

TEST_CASE("Files-from reports missing listed files as errors")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path missing_path = fixture.root / "missing.txt";
    const std::filesystem::path list_path = fixture.root / "missing-files.txt";
    write_file(list_path, missing_path.string() + "\n");

    const std::string output = run_mgrep({
        "--files-from", list_path.string(),
        "needle"
    }, &exit_code);

    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: path does not exist: " + missing_path.string()) != std::string::npos);
}

TEST_CASE("Files-from continues after a missing listed file")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path missing_path = fixture.root / "missing-before-listed-hit.txt";
    const std::filesystem::path list_path = fixture.root / "missing-then-hit-files.txt";
    write_file(
        list_path,
        missing_path.string() + "\n" +
        (fixture.root / "top.txt").string() + "\n"
    );

    const std::string output = run_mgrep({
        "-s",
        "--files-from", list_path.string(),
        "needle"
    }, &exit_code);

    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: path does not exist: " + missing_path.string()) != std::string::npos);
    REQUIRE(output.find("needle at root\n") != std::string::npos);
}

TEST_CASE("Quiet files-from returns success when later listed file matches after an error")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path missing_path = fixture.root / "missing-before-quiet-listed-hit.txt";
    const std::filesystem::path list_path = fixture.root / "missing-then-quiet-hit-files.txt";
    write_file(
        list_path,
        missing_path.string() + "\n" +
        (fixture.root / "top.txt").string() + "\n"
    );

    const std::string output = run_mgrep({
        "-q",
        "--files-from", list_path.string(),
        "needle"
    }, &exit_code);

    REQUIRE(exit_code == 0);
    REQUIRE(output.find("ERROR: path does not exist: " + missing_path.string()) != std::string::npos);
}

TEST_CASE("Quiet files-from stops before later listed error after a match")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path missing_path = fixture.root / "missing-after-quiet-listed-hit.txt";
    const std::filesystem::path list_path = fixture.root / "quiet-hit-then-missing-files.txt";
    write_file(
        list_path,
        (fixture.root / "top.txt").string() + "\n" +
        missing_path.string() + "\n"
    );

    const std::string output = run_mgrep({
        "-q",
        "--files-from", list_path.string(),
        "needle"
    }, &exit_code);

    REQUIRE(exit_code == 0);
    REQUIRE(output.empty());
}

TEST_CASE("Quiet mode stops before later files-from operand after a list match")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path first_list = fixture.root / "quiet-first-list-hit.txt";
    const std::filesystem::path second_list = fixture.root / "quiet-second-list-missing.txt";
    const std::filesystem::path missing_path = fixture.root / "missing-in-second-list.txt";

    write_file(first_list, (fixture.root / "top.txt").string() + "\n");
    write_file(second_list, missing_path.string() + "\n");

    const std::string output = run_mgrep({
        "-q",
        "--files-from", first_list.string(),
        "--files-from", second_list.string(),
        "needle"
    }, &exit_code);

    REQUIRE(exit_code == 0);
    REQUIRE(output.empty());
}

TEST_CASE("Quiet mode stops before later files-from operand after an explicit path match")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path list_path = fixture.root / "quiet-later-files-list.txt";
    const std::filesystem::path missing_path = fixture.root / "missing-in-later-list.txt";

    write_file(list_path, missing_path.string() + "\n");

    const std::string output = run_mgrep({
        "-q",
        "needle",
        (fixture.root / "top.txt").string(),
        "--files-from", list_path.string()
    }, &exit_code);

    REQUIRE(exit_code == 0);
    REQUIRE(output.empty());
}

TEST_CASE("Quiet mode stops before later files-from0 operand after a list match")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path first_list = fixture.root / "quiet-first-list-hit-for-nul.txt";
    const std::filesystem::path second_list = fixture.root / "quiet-second-list-missing.nul";
    const std::filesystem::path missing_path = fixture.root / "missing-in-second-nul-list.txt";

    write_file(first_list, (fixture.root / "top.txt").string() + "\n");
    write_file(second_list, missing_path.string() + '\0');

    const std::string output = run_mgrep({
        "-q",
        "--files-from", first_list.string(),
        "--files-from0", second_list.string(),
        "needle"
    }, &exit_code);

    REQUIRE(exit_code == 0);
    REQUIRE(output.empty());
}

TEST_CASE("Quiet files-from reports listed error when no earlier listed file matches")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path missing_path = fixture.root / "missing-after-quiet-listed-miss.txt";
    const std::filesystem::path list_path = fixture.root / "quiet-miss-then-missing-files.txt";
    write_file(
        list_path,
        (fixture.root / "src" / "two.md").string() + "\n" +
        missing_path.string() + "\n"
    );

    const std::string output = run_mgrep({
        "-q",
        "--files-from", list_path.string(),
        "needle"
    }, &exit_code);

    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: path does not exist: " + missing_path.string()) != std::string::npos);
}

TEST_CASE("Only-matching stdin prints each occurrence on its own line")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "needle one needle\nno match\nneedle\n",
        {"-o", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output == "needle\nneedle\nneedle\n");
}

TEST_CASE("Only-matching empty pattern succeeds without zero-length output")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "alpha\nbeta\n",
        {"-o", ""},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output.empty());
}

TEST_CASE("Only-matching file output keeps path prefix")
{
    CliFixture fixture;

    const std::string output = run_mgrep({
        "-o",
        "needle",
        (fixture.root / "src" / "one.txt").string()
    });

    const std::string expected = display_path(fixture.root / "src" / "one.txt") + "\tneedle\n";
    REQUIRE(output == expected + expected);
}

TEST_CASE("Only-matching output supports line numbers")
{
    CliFixture fixture;

    const std::string output = run_mgrep({
        "-ol",
        "needle",
        (fixture.root / "src" / "one.txt").string()
    });

    REQUIRE(output.find(display_path(fixture.root / "src" / "one.txt") + " 2:\tneedle\n") != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "src" / "one.txt") + " 4:\tneedle\n") != std::string::npos);
}

TEST_CASE("Only-matching output supports colors")
{
    const std::string output = run_mgrep_with_stdin(
        "needle needle\n",
        {"-po", "needle"}
    );

    REQUIRE(output == "\033[38;5;81mneedle\033[0m\n\033[38;5;81mneedle\033[0m\n");
}

TEST_CASE("Count mode overrides only-matching output")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "needle needle\nneedle\n",
        {"-co", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output == "2\n");
}

TEST_CASE("Quiet mode overrides only-matching output")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "needle needle\n",
        {"-qo", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output.empty());
}

TEST_CASE("Invert match prints stdin lines that do not contain the pattern")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "error\nok\nerror again\n",
        {"-v", "error"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output == "ok\n");
}

TEST_CASE("Invert match prints stdin when every line does not contain the pattern")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "alpha\nomega\n",
        {"-v", "needle"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output == "alpha\nomega\n");
}

TEST_CASE("Long invert match supports source and line numbers for files")
{
    CliFixture fixture;

    const std::string output = run_mgrep({
        "--invert-match",
        "-ls",
        "needle",
        (fixture.root / "src" / "one.txt").string()
    });

    REQUIRE(output.find(display_path(fixture.root / "src" / "one.txt")) != std::string::npos);
    REQUIRE(output.find("\n1:\talpha\n") != std::string::npos);
    REQUIRE(output.find("\n3:\tomega\n") != std::string::npos);
    REQUIRE(output.find("needle here") == std::string::npos);
}

TEST_CASE("Invert match default file output prints matching file path once")
{
    CliFixture fixture;

    const std::string output = run_mgrep({
        "-v",
        "needle",
        (fixture.root / "src" / "one.txt").string()
    });

    REQUIRE(output == display_path(fixture.root / "src" / "one.txt") + "\n");
}

TEST_CASE("Invert match file output reports files with only non-matching lines")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path path = fixture.root / "invert-all-lines.txt";
    write_file(path, "alpha\nomega\n");

    const std::string output = run_mgrep({"-v", "needle", path.string()}, &exit_code);

    REQUIRE(exit_code == 0);
    REQUIRE(output == display_path(path) + "\n");
}

TEST_CASE("Invert match count mode counts non-matching lines")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "error\nok\nerror again\n",
        {"-vc", "error"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output == "1\n");
}

TEST_CASE("Invert match quiet mode returns match status without output")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "error\nok\nerror again\n",
        {"-vq", "error"},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output.empty());
}

TEST_CASE("Invert match quiet mode returns no-match status when every line matches")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "error\nerror again\n",
        {"-vq", "error"},
        &exit_code
    );

    REQUIRE(exit_code == 1);
    REQUIRE(output.empty());
}

TEST_CASE("Only-matching cannot be combined with invert match")
{
    int exit_code = -1;
    const std::string output = run_mgrep_with_stdin(
        "error\nok\n",
        {"-vo", "error"},
        &exit_code
    );

    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: --only-matching cannot be used with --invert-match") != std::string::npos);
}

TEST_CASE("Long verbose option preserves verbose output")
{
    CliFixture fixture;

    const std::string output = run_mgrep({
        "--verbose",
        "needle",
        (fixture.root / "top.txt").string()
    });

    REQUIRE(output.find(display_path(fixture.root / "top.txt")) != std::string::npos);
    REQUIRE(output.find("Completed ") != std::string::npos);
}

TEST_CASE("Piped stdin matches long lines across read buffers")
{
    const std::string long_line = std::string(140 * 1024, 'x') + "needle at the end\n";
    const std::string output = run_mgrep_with_stdin(long_line, {"needle at the end"});

    REQUIRE(output == long_line);
}

TEST_CASE("Exit code is zero when a match is found")
{
    CliFixture fixture;
    int exit_code = -1;

    const std::string output = run_mgrep({"-r", "needle", fixture.root.string()}, &exit_code);

    REQUIRE(exit_code == 0);
    REQUIRE_FALSE(output.empty());
}

TEST_CASE("Exit code is one when no match is found")
{
    CliFixture fixture;
    int exit_code = -1;

    const std::string output = run_mgrep({"-r", "not-present", fixture.root.string()}, &exit_code);

    REQUIRE(exit_code == 1);
    REQUIRE(output.empty());
}

TEST_CASE("Exit code is two when a search error occurs")
{
    int exit_code = -1;
    std::ostringstream name;
    name << "mgrep_missing_path_for_exit_code_" << getpid();
    const std::filesystem::path missing_path = std::filesystem::temp_directory_path() / name.str();

    const std::string output = run_mgrep({"needle", missing_path.string()}, &exit_code);

    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: path does not exist: " + missing_path.string()) != std::string::npos);
}

TEST_CASE("Search continues after an explicit missing path")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path missing_path = fixture.root / "missing-before-hit.txt";

    const std::string output = run_mgrep({
        "-s",
        "needle",
        missing_path.string(),
        (fixture.root / "top.txt").string()
    }, &exit_code);

    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: path does not exist: " + missing_path.string()) != std::string::npos);
    REQUIRE(output.find("needle at root\n") != std::string::npos);
}

TEST_CASE("Quiet mode returns success when a later path matches after an error")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path missing_path = fixture.root / "missing-before-quiet-hit.txt";

    const std::string output = run_mgrep({
        "-q",
        "needle",
        missing_path.string(),
        (fixture.root / "top.txt").string()
    }, &exit_code);

    REQUIRE(exit_code == 0);
    REQUIRE(output.find("ERROR: path does not exist: " + missing_path.string()) != std::string::npos);
}

TEST_CASE("Explicit unreadable file returns a search error")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path unreadable = fixture.root / "unreadable.txt";
    write_file(unreadable, "needle\n");
    std::filesystem::permissions(unreadable, std::filesystem::perms::none);

    const std::string output = run_mgrep({"needle", unreadable.string()}, &exit_code);

    std::filesystem::permissions(
        unreadable,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write
    );

    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: could not read file: " + unreadable.string()) != std::string::npos);
}

TEST_CASE("Files-from unreadable file returns a search error")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path unreadable = fixture.root / "unreadable-list-entry.txt";
    const std::filesystem::path list_path = fixture.root / "unreadable-files.txt";
    write_file(unreadable, "needle\n");
    write_file(list_path, unreadable.string() + "\n");
    std::filesystem::permissions(unreadable, std::filesystem::perms::none);

    const std::string output = run_mgrep({
        "--files-from", list_path.string(),
        "needle"
    }, &exit_code);

    std::filesystem::permissions(
        unreadable,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write
    );

    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: could not read file: " + unreadable.string()) != std::string::npos);
}

TEST_CASE("Explicit unreadable directory returns a search error")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path unreadable_dir = fixture.root / "unreadable-dir";
    write_file(unreadable_dir / "hit.txt", "needle\n");
    std::filesystem::permissions(unreadable_dir, std::filesystem::perms::none);

    const std::string output = run_mgrep({"-r", "needle", unreadable_dir.string()}, &exit_code);

    std::filesystem::permissions(
        unreadable_dir,
        std::filesystem::perms::owner_all
    );

    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: could not read directory: " + unreadable_dir.string()) != std::string::npos);
}

TEST_CASE("Color output is enabled by default")
{
    CliFixture fixture;
    int exit_code = -1;

    const std::string output = run_mgrep(
        {"-s", "needle here", (fixture.root / "src" / "one.txt").string()},
        &exit_code,
        test_home_without_ignore(),
        false
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output.find("\033[38;5;75mone.txt\033[0m") != std::string::npos);
    REQUIRE(output.find("\033[38;5;81mneedle here\033[0m") != std::string::npos);
}

TEST_CASE("No-color option disables ANSI output")
{
    CliFixture fixture;
    int exit_code = -1;

    const std::string output = run_mgrep(
        {"--no-color", "needle here", (fixture.root / "src" / "one.txt").string()},
        &exit_code,
        test_home_without_ignore(),
        false
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output.find("\033[") == std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "src" / "one.txt")) != std::string::npos);
}

TEST_CASE("Theme option selects red output")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-rls", "-t", "red", "needle here", fixture.root.string()});

    REQUIRE(output.find("\033[38;5;196mone.txt\033[0m") != std::string::npos);
    REQUIRE(output.find("\033[38;5;124m2:\033[0m\t\033[38;5;217mneedle here\033[0m") != std::string::npos);
}

TEST_CASE("Purple color preset is supported")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-rls", "--theme", "purple", "needle here", fixture.root.string()});

    REQUIRE(output.find("\033[38;5;141mone.txt\033[0m") != std::string::npos);
    REQUIRE(output.find("\033[38;5;98m2:\033[0m\t\033[38;5;177mneedle here\033[0m") != std::string::npos);
}

TEST_CASE("Named editor-style color presets are supported")
{
    CliFixture fixture;

    struct ThemeExpectation {
        const char* name;
        const char* file_color;
        const char* line_color;
        const char* match_color;
    };

    const ThemeExpectation themes[] = {
        {"gruvbox", "\033[38;5;214m", "\033[38;5;108m", "\033[38;5;208m"},
        {"nord", "\033[38;5;110m", "\033[38;5;67m", "\033[38;5;153m"},
        {"dracula", "\033[38;5;117m", "\033[38;5;141m", "\033[38;5;212m"},
        {"nebula", "\033[38;5;183m", "\033[38;5;73m", "\033[38;5;159m"},
    };

    for (const ThemeExpectation& theme : themes) {
        const std::string output = run_mgrep({
            "-rls",
            "--theme",
            theme.name,
            "needle here",
            fixture.root.string()
        });

        REQUIRE(output.find(std::string(theme.file_color) + "one.txt\033[0m") != std::string::npos);
        REQUIRE(output.find(std::string(theme.line_color) + "2:\033[0m\t" +
            theme.match_color + "needle here\033[0m") != std::string::npos);
    }
}

TEST_CASE("Pretty option remains a color-on compatibility alias")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-rls", "--pretty", "needle here", fixture.root.string()});

    REQUIRE(output.find("\033[38;5;75mone.txt\033[0m") != std::string::npos);
    REQUIRE(output.find("\033[38;5;37m2:\033[0m\t\033[38;5;81mneedle here\033[0m") != std::string::npos);
}

TEST_CASE("Color-name pattern is not consumed as a theme")
{
    CliFixture fixture;
    int exit_code = -1;

    write_file(fixture.root / "red.txt", "red\nblue\n");

    const std::string output = run_mgrep(
        {"red", (fixture.root / "red.txt").string()},
        &exit_code
    );

    REQUIRE(exit_code == 0);
    REQUIRE(output.find(display_path(fixture.root / "red.txt")) != std::string::npos);
}

TEST_CASE("Short count option no longer consumes color presets")
{
    CliFixture fixture;

    write_file(fixture.root / "red.txt", "red\nnot red\n");

    const std::string output = run_mgrep({"-rc", "red", fixture.root.string()});

    REQUIRE(output.find(display_path(fixture.root / "red.txt") + "\t2\n") != std::string::npos);
    REQUIRE(output.find("\033[38;5;196m") == std::string::npos);
}

TEST_CASE("Before and after context are printed around matched lines")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-rs", "-B", "1", "-A", "1", "needle here", fixture.root.string()});

    REQUIRE(output.find("alpha\n") != std::string::npos);
    REQUIRE(output.find("\nneedle here") != std::string::npos);
    REQUIRE(output.find("omega\n") != std::string::npos);
}

TEST_CASE("File context output includes matched source without source option")
{
    CliFixture fixture;
    const std::filesystem::path path = fixture.root / "context-without-source.txt";
    write_file(path, "before\nneedle here\nafter\n");

    const std::string output = run_mgrep({
        "-B", "1",
        "-A", "1",
        "needle",
        path.string()
    });

    const std::string expected =
        "before\n" +
        display_path(path) + "\nneedle here\n"
        "after\n";
    REQUIRE(output == expected);
}

TEST_CASE("File context output includes matched line number without source option")
{
    CliFixture fixture;
    const std::filesystem::path path = fixture.root / "context-line-without-source.txt";
    write_file(path, "before\nneedle here\nafter\n");

    const std::string output = run_mgrep({
        "-l",
        "-B", "1",
        "-A", "1",
        "needle",
        path.string()
    });

    const std::string expected =
        "before\n" +
        display_path(path) + "\n2:\tneedle here\n"
        "after\n";
    REQUIRE(output == expected);
}

TEST_CASE("Overlapping file context does not duplicate source lines")
{
    CliFixture fixture;
    const std::filesystem::path path = fixture.root / "context-overlap.txt";
    write_file(path, "before\nneedle one\nmiddle\nneedle two\nafter\n");

    const std::string output = run_mgrep({
        "-s",
        "-B", "1",
        "-A", "1",
        "needle",
        path.string()
    });

    const std::string expected =
        "before\n" +
        display_path(path) + "\nneedle one\n"
        "middle\n" +
        display_path(path) + "\nneedle two\n"
        "after\n";
    REQUIRE(output == expected);
}

TEST_CASE("Overlapping stdin context does not duplicate source lines")
{
    const std::string output = run_mgrep_with_stdin(
        "before\nneedle one\nmiddle\nneedle two\nafter\n",
        {"-B", "1", "-A", "1", "needle"}
    );

    REQUIRE(output == "before\nneedle one\nmiddle\nneedle two\nafter\n");
}

TEST_CASE("Max-lines option stops scanning after the requested line count")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-rls", "-m", "1", "needle", fixture.root.string()});

    REQUIRE(output.find("needle here") == std::string::npos);
    REQUIRE(output.find("needle nested") == std::string::npos);
}

TEST_CASE("Invalid numeric options are rejected")
{
    int exit_code = -1;

    std::string output = run_mgrep_with_stdin(
        "needle\n",
        {"-A", "-1", "needle"},
        &exit_code
    );
    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: invalid after-context value: -1") != std::string::npos);

    output = run_mgrep_with_stdin(
        "needle\n",
        {"-B", "abc", "needle"},
        &exit_code
    );
    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: invalid before-context value: abc") != std::string::npos);

    output = run_mgrep_with_stdin(
        "needle\n",
        {"-m", "abc", "needle"},
        &exit_code
    );
    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: invalid max-lines value: abc") != std::string::npos);

    output = run_mgrep_with_stdin(
        "needle\n",
        {"-m", "-1", "needle"},
        &exit_code
    );
    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: invalid max-lines value: -1") != std::string::npos);

    output = run_mgrep_with_stdin(
        "needle\n",
        {"-A", " 1", "needle"},
        &exit_code
    );
    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: invalid after-context value:  1") != std::string::npos);

    output = run_mgrep_with_stdin(
        "needle\n",
        {"-m", "+1", "needle"},
        &exit_code
    );
    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: invalid max-lines value: +1") != std::string::npos);

    output = run_mgrep_with_stdin(
        "needle\n",
        {"--type", "unknown", "needle"},
        &exit_code
    );
    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: invalid type: unknown") != std::string::npos);

    output = run_mgrep_with_stdin(
        "needle\n",
        {"--ext", "h,", "needle"},
        &exit_code
    );
    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: invalid extension list: h,") != std::string::npos);

    output = run_mgrep_with_stdin(
        "needle\n",
        {"--glob", "", "needle"},
        &exit_code
    );
    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: invalid glob: ") != std::string::npos);

    output = run_mgrep_with_stdin(
        "needle\n",
        {"--exclude-glob", "", "needle"},
        &exit_code
    );
    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: invalid exclude glob: ") != std::string::npos);
}

TEST_CASE("Non-recursive directory search does not descend into child directories")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"needle", fixture.root.string()});

    REQUIRE(output.find(display_path(fixture.root / "top.txt")) != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "src" / "one.txt")) == std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "src" / "nested" / "three.cpp")) == std::string::npos);
}

TEST_CASE("Directory operands preserve command-line order")
{
    CliFixture fixture;
    int exit_code = -1;
    const std::filesystem::path first_dir = fixture.root / "first-dir";
    const std::filesystem::path second_dir = fixture.root / "second-dir";

    for (int i = 0; i < 64; ++i) {
        write_file(
            first_dir / ("slow_" + std::to_string(i) + ".txt"),
            std::string(256 * 1024, 'x') + "needle first " + std::to_string(i) + "\n"
        );
    }

    for (int i = 0; i < 64; ++i) {
        write_file(
            second_dir / ("fast_" + std::to_string(i) + ".txt"),
            "needle second " + std::to_string(i) + "\n"
        );
    }

    const std::string output = run_mgrep({
        "-rs",
        "needle",
        first_dir.string(),
        second_dir.string()
    }, &exit_code);

    const size_t first_pos = output.find("needle first ");
    const size_t second_pos = output.find("needle second ");

    REQUIRE(exit_code == 0);
    REQUIRE(first_pos != std::string::npos);
    REQUIRE(second_pos != std::string::npos);
    REQUIRE(first_pos < second_pos);
}

TEST_CASE("Recursive search does not follow symlinked directories")
{
    CliFixture fixture;

    write_file(fixture.root / "link_target" / "linked.txt",
               "needle through symlink\n");

    std::error_code ec;
    std::filesystem::create_directory_symlink(
        fixture.root / "link_target",
        fixture.root / "visible_link",
        ec
    );
    if (ec) {
        return;
    }

    const std::string output = run_mgrep({"-r", "needle through symlink", fixture.root.string()});

    REQUIRE(output.find(display_path(fixture.root / "link_target" / "linked.txt")) != std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "visible_link" / "linked.txt")) == std::string::npos);
}

TEST_CASE("Directory traversal searches symlinks to regular files")
{
    CliFixture fixture;
    const std::filesystem::path outside_dir = fixture.root / "outside-target";
    const std::filesystem::path search_dir = fixture.root / "symlink-search";
    const std::filesystem::path target = outside_dir / "target.txt";
    const std::filesystem::path link = search_dir / "linked.txt";

    write_file(target, "needle through linked file\n");
    std::filesystem::create_directories(search_dir);

    std::error_code ec;
    std::filesystem::create_symlink(target, link, ec);
    if (ec) {
        return;
    }

    const std::string output = run_mgrep({"-r", "needle through linked file", search_dir.string()});

    REQUIRE(output.find(display_path(link)) != std::string::npos);
    REQUIRE(output.find(display_path(target)) == std::string::npos);
}

TEST_CASE("Patterns are treated as fixed strings, not regular expressions")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-rls", "int main(", fixture.root.string()});

    REQUIRE(output.find(display_path(fixture.root / "src" / "regex_chars.txt")) != std::string::npos);
    REQUIRE(output.find("1:\tliteral int main( should match") != std::string::npos);
}

TEST_CASE("Pattern backslash escapes are decoded by default")
{
    int exit_code = -1;

    const std::string output = run_mgrep_with_stdin(
        "alpha\nbeta\n",
        {"-q", "alpha\\nbeta"},
        &exit_code
    );

    REQUIRE(output.empty());
    REQUIRE(exit_code == 0);
}

TEST_CASE("Escaped punctuation remains fixed-string matching")
{
    int exit_code = -1;

    const std::string output = run_mgrep_with_stdin(
        "literal int main( should match\n",
        {"-q", "int main\\("},
        &exit_code
    );

    REQUIRE(output.empty());
    REQUIRE(exit_code == 0);
}

TEST_CASE("Double backslash searches for a literal backslash")
{
    int exit_code = -1;

    const std::string output = run_mgrep_with_stdin(
        "alpha\\nbeta\n",
        {"-q", "alpha\\\\nbeta"},
        &exit_code
    );

    REQUIRE(output.empty());
    REQUIRE(exit_code == 0);
}

TEST_CASE("Literal option disables pattern escape decoding")
{
    int exit_code = -1;

    std::string output = run_mgrep_with_stdin(
        "alpha\nbeta\n",
        {"--literal", "-q", "alpha\\nbeta"},
        &exit_code
    );
    REQUIRE(output.empty());
    REQUIRE(exit_code == 1);

    output = run_mgrep_with_stdin(
        "alpha\\nbeta\n",
        {"--literal", "-q", "alpha\\nbeta"},
        &exit_code
    );
    REQUIRE(output.empty());
    REQUIRE(exit_code == 0);
}

TEST_CASE("Hex pattern escapes are decoded by default")
{
    int exit_code = -1;

    const std::string output = run_mgrep_with_stdin(
        "hex: A\n",
        {"-q", "hex: \\x41"},
        &exit_code
    );

    REQUIRE(output.empty());
    REQUIRE(exit_code == 0);
}

TEST_CASE("NUL pattern escape is decoded by default")
{
    int exit_code = -1;

    const std::string output = run_mgrep_with_stdin(
        std::string("alpha\0beta\n", 11),
        {"-qa", "alpha\\0beta"},
        &exit_code
    );

    REQUIRE(output.empty());
    REQUIRE(exit_code == 0);
}

TEST_CASE("Files without a trailing newline still report source matches")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-rls", "final needle", fixture.root.string()});

    REQUIRE(output.find(display_path(fixture.root / "src" / "no_newline.txt")) != std::string::npos);
    REQUIRE(output.find("1:\tfinal needle without newline") != std::string::npos);
}

TEST_CASE("Long lines larger than the read buffer are searched correctly")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-r", "needle at the end", fixture.root.string()});

    REQUIRE(output.find(display_path(fixture.root / "src" / "long_line.txt")) != std::string::npos);
}

TEST_CASE("Advanced color overrides replace preset component colors")
{
    CliFixture fixture;

    const std::string output = run_mgrep({
        "-rls",
        "-t", "red",
        "--colors", "match:fg:magenta",
        "--colors", "line:fg:cyan",
        "needle here",
        fixture.root.string()
    });

    REQUIRE(output.find("\033[38;5;51m2:\033[0m") != std::string::npos);
    REQUIRE(output.find("\033[38;5;201mneedle here\033[0m") != std::string::npos);
}

TEST_CASE("Advanced color styles compose with color overrides")
{
    CliFixture fixture;

    const std::string output = run_mgrep({
        "-rls",
        "--colors", "match:style:bold",
        "--colors", "match:fg:cyan",
        "needle here",
        fixture.root.string()
    });

    REQUIRE(output.find("\033[1m\033[38;5;51mneedle here\033[0m") != std::string::npos);
}

TEST_CASE("Invalid theme fails before searching")
{
    CliFixture fixture;
    int exit_code = 0;

    const std::string output = run_mgrep({
        "--theme", "not-a-theme",
        "needle",
        fixture.root.string()
    }, &exit_code);

    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: invalid theme: not-a-theme") != std::string::npos);
}

TEST_CASE("Invalid advanced color overrides fail before searching")
{
    CliFixture fixture;
    int exit_code = 0;

    const std::string output = run_mgrep({
        "--colors", "match:nope:red",
        "needle",
        fixture.root.string()
    }, &exit_code);

    REQUIRE(exit_code == 2);
    REQUIRE(output.find("ERROR: invalid --colors value: match:nope:red") != std::string::npos);
}
