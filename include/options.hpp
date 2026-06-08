#pragma once

#include "colors.hpp"
#include "ignore.hpp"

#include <string>

inline constexpr int MGREP_EXIT_MATCH_FOUND = 0;
inline constexpr int MGREP_EXIT_NO_MATCH = 1;
inline constexpr int MGREP_EXIT_ERROR = 2;

struct UserOptions {
    std::string pattern = "";
    ColorTheme colors;
    IgnoreRules ignore_rules;
    bool recursive_mode = false;
    bool is_verbose = false;
    bool cool_colors = false;
    bool source_print = false;
    bool line_number_print = false;
    bool add_newline = false;
    bool all_files = false;
    bool count_print = false;
    unsigned int print_before_source = 0;
    unsigned int print_after_source = 0;
    unsigned int max_lines = 0;
};

struct ParseResult {
    bool ok = true;
    int exit_code = MGREP_EXIT_MATCH_FOUND;
    int first_path_arg = 0;
};

void printHelp(char* file_name);
ParseResult parse_user_options(int argc, char* argv[], UserOptions& user_stats);
