#include "ignore.hpp"
#include "options.hpp"
#include "search.hpp"
#include "traversal.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <istream>
#include <unistd.h>

using std::cout;

bool check_user_root(std::filesystem::path& root)
{
    std::error_code ec;

    if (!std::filesystem::exists(root, ec)) {
        std::cerr << "ERROR: path does not exist: " << root.string() << "\n";
        return false;
    }

    return true;
}

bool add_explicit_file(const std::string& path, SearchWork& work)
{
    std::error_code ec;

    if (!std::filesystem::exists(path, ec)) {
        std::cerr << "ERROR: path does not exist: " << path << "\n";
        return false;
    }

    if (!std::filesystem::is_regular_file(path, ec)) {
        std::cerr << "ERROR: path is not a regular file: " << path << "\n";
        return false;
    }

    add_search_path(work, path);
    return true;
}

bool process_file_list_stream(std::istream& input, char delimiter, SearchWork& work)
{
    std::string path;

    while (std::getline(input, path, delimiter)) {
        if (path.empty()) {
            continue;
        }

        if (!add_explicit_file(path, work)) {
            return false;
        }
    }

    return true;
}

bool process_file_list(const std::string& list_path, char delimiter, SearchWork& work)
{
    if (list_path == "-") {
        return process_file_list_stream(std::cin, delimiter, work);
    }

    std::ifstream input(list_path, std::ios::binary);
    if (!input) {
        std::cerr << "ERROR: could not open file list: " << list_path << "\n";
        return false;
    }

    return process_file_list_stream(input, delimiter, work);
}

int main(int argc, char* argv[])
{
    std::ios::sync_with_stdio(false);
    cout.tie(nullptr);

    UserOptions user_stats;
    const ParseResult parse_result = parse_user_options(argc, argv, user_stats);
    if (!parse_result.ok || user_stats.pattern.empty()) {
        return parse_result.exit_code;
    }

    int first_path_arg = parse_result.first_path_arg;

    const bool has_file_list_input =
        !user_stats.files_from.empty() || !user_stats.files_from0.empty();
    const bool has_path_args = first_path_arg < argc;
    if (!has_file_list_input && !has_path_args && !::isatty(STDIN_FILENO)) {
        search_stdin(user_stats);

        if (user_stats.is_verbose && !user_stats.quiet) {
            cout << "Completed 1 stdin read and found " << matches << " matches\n";
        }

        return exit_code_from_matches();
    }

    if (!user_stats.all_files) {
        load_home_ignore(user_stats.ignore_rules);
    }

    SearchWork work(user_stats);
    work.read_file = choose_read_file(user_stats);
    bool had_error = false;

    for (const auto& list_path : user_stats.files_from) {
        if (!process_file_list(list_path, '\n', work)) {
            had_error = true;
            break;
        }
    }

    if (!had_error) {
        for (const auto& list_path : user_stats.files_from0) {
            if (!process_file_list(list_path, '\0', work)) {
                had_error = true;
                break;
            }
        }
    }

    for (int i = first_path_arg; i < argc; ++i) {
        if (had_error) {
            break;
        }

        if (std::strcmp(argv[i], "-") == 0) {
            search_stdin(user_stats);
            continue;
        }

        std::filesystem::path root = argv[i];

        if (check_user_root(root)) {
            collect_search_files(root.string(), work);
        } else {
            had_error = true;
            break;
        }
    }

    const size_t completed_batches = finish_search(work);

    if (user_stats.is_verbose && !user_stats.quiet) {
        cout << "Completed " << completed_batches << " batches and found " << matches << " matches\n";
    }

    if (had_error) {
        return MGREP_EXIT_ERROR;
    }

    return exit_code_from_matches();
}
