#include "ignore.hpp"
#include "options.hpp"
#include "search.hpp"
#include "traversal.hpp"

#include <cstring>
#include <filesystem>
#include <functional>
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

    if (std::filesystem::is_regular_file(root, ec) && ::access(root.c_str(), R_OK) != 0) {
        std::cerr << "ERROR: could not read file: " << root.string() << "\n";
        return false;
    }

    if (std::filesystem::is_directory(root, ec) && ::access(root.c_str(), R_OK | X_OK) != 0) {
        std::cerr << "ERROR: could not read directory: " << root.string() << "\n";
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

    if (::access(path.c_str(), R_OK) != 0) {
        std::cerr << "ERROR: could not read file: " << path << "\n";
        return false;
    }

    if (!path_filter_allows(path, work.user_stats)) {
        return true;
    }

    if (work.user_stats.list_files) {
        add_search_path(work, path);
    } else {
        work.small_paths.push_back(path);
    }
    return true;
}

bool process_file_list_stream(
    std::istream& input,
    char delimiter,
    SearchWork& work,
    const std::function<bool()>& stop_after_add
)
{
    std::string path;
    bool had_error = false;

    while (std::getline(input, path, delimiter)) {
        if (delimiter == '\n' && !path.empty() && path.back() == '\r') {
            path.pop_back();
        }

        if (path.empty()) {
            continue;
        }

        if (!add_explicit_file(path, work)) {
            had_error = true;
        } else if (stop_after_add()) {
            return !had_error;
        }
    }

    return !had_error;
}

bool process_file_list(
    const std::string& list_path,
    char delimiter,
    SearchWork& work,
    const std::function<bool()>& stop_after_add
)
{
    if (list_path == "-") {
        return process_file_list_stream(std::cin, delimiter, work, stop_after_add);
    }

    std::ifstream input(list_path, std::ios::binary);
    if (!input) {
        std::cerr << "ERROR: could not open file list: " << list_path << "\n";
        return false;
    }

    return process_file_list_stream(input, delimiter, work, stop_after_add);
}

int main(int argc, char* argv[])
{
    std::ios::sync_with_stdio(false);
    cout.tie(nullptr);

    UserOptions user_stats;
    const ParseResult parse_result = parse_user_options(argc, argv, user_stats);
    if (!parse_result.ok) {
        return parse_result.exit_code;
    }

    const bool has_file_list_input =
        !user_stats.file_lists.empty();
    bool has_path_args = false;
    for (const auto& operand : user_stats.input_operands) {
        if (operand.kind == InputOperand::Kind::Path) {
            has_path_args = true;
            break;
        }
    }
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
    size_t completed_batches = 0;

    auto finish_pending_work = [&]() {
        completed_batches += finish_search(work);
        work.small_paths.clear();
        work.batch.clear();
        work.tp.reset();
        work.stop_requested.store(false, std::memory_order_relaxed);
        work.read_file = choose_read_file(user_stats);
    };

    auto has_pending_work = [&]() {
        return work.tp != nullptr || !work.small_paths.empty() || !work.batch.empty();
    };

    auto stop_quiet_after_file_list_add = [&]() {
        if (!user_stats.quiet || user_stats.list_files) {
            return false;
        }
        finish_pending_work();
        return matches > 0;
    };

    for (const auto& operand : user_stats.input_operands) {
        if (user_stats.quiet && matches > 0) {
            break;
        }

        if (user_stats.quiet && has_pending_work()) {
            finish_pending_work();
            if (matches > 0) {
                break;
            }
        }

        if (operand.kind == InputOperand::Kind::FileList) {
            if (!process_file_list(
                    operand.value,
                    operand.delimiter,
                    work,
                    stop_quiet_after_file_list_add)) {
                had_error = true;
            }
            continue;
        }

        if (operand.value == "-") {
            finish_pending_work();
            if (user_stats.quiet && matches > 0) {
                break;
            }
            search_stdin(user_stats);
            if (user_stats.quiet && matches > 0) {
                break;
            }
            continue;
        }

        std::filesystem::path root = operand.value;

        if (check_user_root(root)) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(root, ec)) {
                if (work.tp) {
                    finish_pending_work();
                }
                if (path_filter_allows(root.string(), user_stats)) {
                    if (user_stats.list_files) {
                        add_search_path(work, root.string());
                    } else {
                        work.small_paths.push_back(root.string());
                    }
                }
            } else {
                if (has_pending_work()) {
                    finish_pending_work();
                }
                collect_search_files(root.string(), work);
            }
        } else {
            had_error = true;
        }
    }

    finish_pending_work();

    if (user_stats.is_verbose && !user_stats.quiet) {
        cout << "Completed " << completed_batches << " batches and found " << matches << " matches\n";
    }

    if (had_error && !(user_stats.quiet && matches > 0)) {
        return MGREP_EXIT_ERROR;
    }

    if (user_stats.list_files) {
        return MGREP_EXIT_MATCH_FOUND;
    }

    return exit_code_from_matches();
}
