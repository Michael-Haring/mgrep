#pragma once

#include "colors.hpp"
#include "ignore.hpp"

#include <string>
#include <string_view>
#include <memory>
#include <utility>
#include <vector>

namespace re2 {
class RE2;
}

inline constexpr int MGREP_EXIT_MATCH_FOUND = 0;
inline constexpr int MGREP_EXIT_NO_MATCH = 1;
inline constexpr int MGREP_EXIT_ERROR = 2;

struct InputOperand {
    enum class Kind {
        Path,
        FileList,
        Stdin
    };

    Kind kind = Kind::Path;
    std::string value;
    char delimiter = '\n';
};

struct UserOptions {
    std::string pattern = "";
    std::string folded_pattern = "";
    std::shared_ptr<const re2::RE2> compiled_regex;
    std::vector<std::string> allowed_extensions;
    std::vector<std::string> include_globs;
    std::vector<std::string> exclude_globs;
    std::vector<std::string> files_from;
    std::vector<std::string> files_from0;
    std::vector<std::pair<std::string, char>> file_lists;
    std::vector<InputOperand> input_operands;
    ColorTheme colors;
    IgnoreRules ignore_rules;
    bool recursive_mode = false;
    bool is_verbose = false;
    bool cool_colors = true;
    bool source_print = false;
    bool line_number_print = false;
    bool add_newline = false;
    bool all_files = false;
    bool count_print = false;
    bool quiet = false;
    bool only_matching = false;
    bool one_line = false;
    bool invert_match = false;
    bool ignore_case = false;
    bool regex_pattern = false;
    bool heading = false;
    bool list_files = false;
    unsigned int print_before_source = 0;
    unsigned int print_after_source = 0;
    unsigned int max_lines = 0;
};

struct ParseResult {
    bool ok = true;
    int exit_code = MGREP_EXIT_MATCH_FOUND;
    int first_path_arg = 0;
};

void printHelp(const char* file_name, const UserOptions& user_stats);
ParseResult parse_user_options(int argc, char* argv[], UserOptions& user_stats);
void apply_terminal_output_defaults(UserOptions& user_stats, bool stdout_is_terminal);
bool extension_filter_allows(std::string_view path, const UserOptions& user_stats);
bool exclude_glob_matches_path(std::string_view path, const UserOptions& user_stats);
bool exclude_glob_matches_directory(std::string_view path, const UserOptions& user_stats);
bool path_filter_allows(std::string_view path, const UserOptions& user_stats);
