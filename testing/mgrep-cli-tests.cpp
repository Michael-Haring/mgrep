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
    const std::string& home_dir = test_home_without_ignore()
)
{
    std::string command = "HOME=";
    command += shell_quote(home_dir);
    command += " ./mgrep";
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
    int* exit_code = nullptr
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
    REQUIRE(output.find((fixture.root / "skip.bin").string()) == std::string::npos);
    REQUIRE(output.find((fixture.root / "no_extension").string()) == std::string::npos);
    REQUIRE(output.find((fixture.root / ".git" / "hidden.txt").string()) == std::string::npos);
    REQUIRE(output.find((fixture.root / "build" / "generated.txt").string()) == std::string::npos);
    REQUIRE(output.find((fixture.root / "binary.dat").string()) == std::string::npos);
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
    REQUIRE(output.find((fixture.root / "ignored.txt").string()) == std::string::npos);
    REQUIRE(output.find((fixture.root / "report.generated").string()) == std::string::npos);
    REQUIRE(output.find((fixture.root / "ignored_dir" / "hit.txt").string()) == std::string::npos);
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

    REQUIRE(output.find((fixture.root / "testing-venv" / "hit.txt").string()) == std::string::npos);
    REQUIRE(output.find((fixture.root / "testing_venv" / "hit.txt").string()) == std::string::npos);
    REQUIRE(output.find(display_path(fixture.root / "visible-env" / "hit.txt")) != std::string::npos);
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

TEST_CASE("Color preset argument is consumed before the pattern")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-rpls", "red", "needle here", fixture.root.string()});

    REQUIRE(output.find("\033[38;5;203mone.txt\033[0m") != std::string::npos);
    REQUIRE(output.find("\033[38;5;167m2:\033[0m\t\033[38;5;210mneedle here\033[0m") != std::string::npos);
}

TEST_CASE("Purple color preset is supported")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-rpls", "purple", "needle here", fixture.root.string()});

    REQUIRE(output.find("\033[38;5;141mone.txt\033[0m") != std::string::npos);
    REQUIRE(output.find("\033[38;5;98m2:\033[0m\t\033[38;5;177mneedle here\033[0m") != std::string::npos);
}

TEST_CASE("Long pretty option consumes color preset before the pattern")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-rls", "--pretty", "red", "needle here", fixture.root.string()});

    REQUIRE(output.find("\033[38;5;203mone.txt\033[0m") != std::string::npos);
    REQUIRE(output.find("\033[38;5;167m2:\033[0m\t\033[38;5;210mneedle here\033[0m") != std::string::npos);
}

TEST_CASE("Short count option no longer consumes color presets")
{
    CliFixture fixture;

    write_file(fixture.root / "red.txt", "red\nnot red\n");

    const std::string output = run_mgrep({"-rc", "red", fixture.root.string()});

    REQUIRE(output.find(display_path(fixture.root / "red.txt") + "\t2\n") != std::string::npos);
    REQUIRE(output.find("\033[38;5;203m") == std::string::npos);
}

TEST_CASE("Before and after context are printed around matched lines")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-rs", "-B", "1", "-A", "1", "needle here", fixture.root.string()});

    REQUIRE(output.find("alpha\n") != std::string::npos);
    REQUIRE(output.find("\nneedle here") != std::string::npos);
    REQUIRE(output.find("omega\n") != std::string::npos);
}

TEST_CASE("Max-lines option stops scanning after the requested line count")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-rls", "-m", "1", "needle", fixture.root.string()});

    REQUIRE(output.find("needle here") == std::string::npos);
    REQUIRE(output.find("needle nested") == std::string::npos);
}

TEST_CASE("Non-recursive directory search does not descend into child directories")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"needle", fixture.root.string()});

    REQUIRE(output.find(display_path(fixture.root / "top.txt")) != std::string::npos);
    REQUIRE(output.find((fixture.root / "src" / "one.txt").string()) == std::string::npos);
    REQUIRE(output.find((fixture.root / "src" / "nested" / "three.cpp").string()) == std::string::npos);
}

TEST_CASE("Recursive search does not follow symlinked directories")
{
    CliFixture fixture;

    write_file(fixture.root / ".hidden_target" / "linked.txt",
               "needle through symlink\n");

    std::error_code ec;
    std::filesystem::create_directory_symlink(
        fixture.root / ".hidden_target",
        fixture.root / "visible_link",
        ec
    );
    if (ec) {
        return;
    }

    const std::string output = run_mgrep({"-r", "needle through symlink", fixture.root.string()});

    REQUIRE(output.find((fixture.root / "visible_link" / "linked.txt").string()) == std::string::npos);
    REQUIRE(output.find((fixture.root / ".hidden_target" / "linked.txt").string()) == std::string::npos);
}

TEST_CASE("Patterns are treated as fixed strings, not regular expressions")
{
    CliFixture fixture;

    const std::string output = run_mgrep({"-rls", "int main(", fixture.root.string()});

    REQUIRE(output.find(display_path(fixture.root / "src" / "regex_chars.txt")) != std::string::npos);
    REQUIRE(output.find("1:\tliteral int main( should match") != std::string::npos);
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
        "-rpls",
        "--colors", "match:fg:magenta",
        "--colors", "line:fg:cyan",
        "red",
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
        "-rpls",
        "--colors", "match:style:bold",
        "--colors", "match:fg:cyan",
        "needle here",
        fixture.root.string()
    });

    REQUIRE(output.find("\033[1m\033[38;5;51mneedle here\033[0m") != std::string::npos);
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
