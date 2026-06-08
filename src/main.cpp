#include "ignore.hpp"
#include "options.hpp"
#include "search.hpp"
#include "traversal.hpp"

#include <cstring>
#include <filesystem>
#include <iostream>
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

    if (first_path_arg == argc && !::isatty(STDIN_FILENO)) {
        search_stdin(user_stats);

        if (user_stats.is_verbose) {
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

    for (int i = first_path_arg; i < argc; ++i) {
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

    if (user_stats.is_verbose) {
        cout << "Completed " << completed_batches << " batches and found " << matches << " matches\n";
    }

    if (had_error) {
        return MGREP_EXIT_ERROR;
    }

    return exit_code_from_matches();
}
