#include "options.hpp"

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <getopt.h>
#include <iostream>
#include <vector>

constexpr char* OPTIONS = (char*)"vhrcpnlsaqoit:B:A:m:";
constexpr int COLORS_OPTION = 1000;
constexpr int COUNT_OPTION = 1001;
constexpr int PRETTY_OPTION = 1002;
constexpr int QUIET_OPTION = 1003;
constexpr int FILES_FROM_OPTION = 1004;
constexpr int FILES_FROM0_OPTION = 1005;
constexpr int ONLY_MATCHING_OPTION = 1006;
constexpr int INVERT_MATCH_OPTION = 1007;
constexpr int VERBOSE_OPTION = 1008;
constexpr int THEME_OPTION = 1009;
constexpr int NO_COLOR_OPTION = 1010;
constexpr int IGNORE_CASE_OPTION = 1011;
constexpr int LITERAL_OPTION = 1012;
constexpr int TYPE_OPTION = 1013;
constexpr int EXT_OPTION = 1014;
constexpr int GLOB_OPTION = 1015;
constexpr int EXCLUDE_GLOB_OPTION = 1016;
constexpr int HEADING_OPTION = 1017;
constexpr int FILES_OPTION = 1018;

using std::cout;

bool short_option_takes_argument(char option)
{
    return option == 't' || option == 'B' || option == 'A' || option == 'm';
}

bool long_option_takes_argument(std::string_view option)
{
    return option == "--colors" ||
        option == "--files-from" ||
        option == "--files-from0" ||
        option == "--null-files-from" ||
        option == "--theme" ||
        option == "--type" ||
        option == "--ext" ||
        option == "--glob" ||
        option == "--exclude-glob";
}

bool append_file_list_operand(
    std::string_view option,
    const std::vector<std::string>& original_args,
    size_t& index,
    UserOptions& user_stats
)
{
    const size_t equals_pos = option.find('=');
    const std::string_view name = equals_pos == std::string_view::npos
        ? option
        : option.substr(0, equals_pos);

    const bool is_files_from = name == "--files-from";
    const bool is_files_from0 = name == "--files-from0" || name == "--null-files-from";
    if (!is_files_from && !is_files_from0) {
        return false;
    }

    std::string value;
    if (equals_pos != std::string_view::npos) {
        value.assign(option.substr(equals_pos + 1));
    } else if (index + 1 < original_args.size()) {
        ++index;
        value = original_args[index];
    }

    user_stats.input_operands.push_back({
        InputOperand::Kind::FileList,
        std::move(value),
        is_files_from0 ? '\0' : '\n'
    });
    return true;
}

void record_input_operands(const std::vector<std::string>& original_args, UserOptions& user_stats)
{
    bool pattern_seen = false;
    bool options_ended = false;

    for (size_t i = 0; i < original_args.size(); ++i) {
        const std::string& arg = original_args[i];

        if (!options_ended && arg == "--") {
            options_ended = true;
            continue;
        }

        if (!options_ended && arg.size() > 2 && arg.rfind("--", 0) == 0) {
            if (append_file_list_operand(arg, original_args, i, user_stats)) {
                continue;
            }

            const size_t equals_pos = arg.find('=');
            const std::string_view name = equals_pos == std::string::npos
                ? std::string_view(arg)
                : std::string_view(arg.data(), equals_pos);
            if (equals_pos == std::string::npos && long_option_takes_argument(name) &&
                i + 1 < original_args.size()) {
                ++i;
            }
            continue;
        }

        if (!options_ended && arg.size() > 1 && arg[0] == '-' && arg != "-") {
            for (size_t pos = 1; pos < arg.size(); ++pos) {
                if (short_option_takes_argument(arg[pos])) {
                    if (pos + 1 == arg.size() && i + 1 < original_args.size()) {
                        ++i;
                    }
                    break;
                }
            }
            continue;
        }

        if (!user_stats.list_files && !pattern_seen) {
            pattern_seen = true;
            continue;
        }

        user_stats.input_operands.push_back({InputOperand::Kind::Path, arg, '\n'});
    }
}

bool starts_with_digit(const char* value)
{
    return value != nullptr && value[0] >= '0' && value[0] <= '9';
}

bool parse_nonnegative_option(const char* value, unsigned int& output)
{
    if (!starts_with_digit(value)) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (errno == ERANGE || end == value || *end != '\0' || parsed > UINT_MAX) {
        return false;
    }

    output = static_cast<unsigned int>(parsed);
    return true;
}

bool parse_max_lines_option(const char* value, unsigned int& output)
{
    if (!starts_with_digit(value)) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (errno == ERANGE || end == value || *end != '\0' || parsed > UINT_MAX) {
        return false;
    }

    output = parsed <= 0 ? 0 : static_cast<unsigned int>(parsed);
    return true;
}

char fold_ascii(char ch)
{
    return ch >= 'A' && ch <= 'Z'
        ? static_cast<char>(ch + ('a' - 'A'))
        : ch;
}

std::string fold_ascii_string(const std::string& value)
{
    std::string folded;
    folded.resize(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        folded[i] = fold_ascii(value[i]);
    }
    return folded;
}

char lower_ascii(char ch)
{
    return ch >= 'A' && ch <= 'Z'
        ? static_cast<char>(ch + ('a' - 'A'))
        : ch;
}

void add_allowed_extension(UserOptions& user_stats, std::string_view ext)
{
    if (ext.empty()) {
        return;
    }

    std::string normalized;
    normalized.reserve(ext.size() + (ext.front() == '.' ? 0 : 1));
    if (ext.front() != '.') {
        normalized.push_back('.');
    }

    for (char ch : ext) {
        normalized.push_back(lower_ascii(ch));
    }

    user_stats.allowed_extensions.push_back(std::move(normalized));
}

bool parse_extension_list(std::string_view value, UserOptions& user_stats)
{
    while (!value.empty()) {
        const size_t comma_pos = value.find(',');
        const std::string_view ext = comma_pos == std::string_view::npos
            ? value
            : value.substr(0, comma_pos);

        if (ext.empty()) {
            return false;
        }
        add_allowed_extension(user_stats, ext);

        if (comma_pos == std::string_view::npos) {
            break;
        }
        if (comma_pos + 1 >= value.size()) {
            return false;
        }
        value.remove_prefix(comma_pos + 1);
    }

    return true;
}

bool parse_file_type(std::string_view type, UserOptions& user_stats)
{
    if (type == "header" || type == "headers") {
        add_allowed_extension(user_stats, "h");
        add_allowed_extension(user_stats, "hh");
        add_allowed_extension(user_stats, "hpp");
        add_allowed_extension(user_stats, "hxx");
        return true;
    }
    if (type == "source" || type == "sources") {
        add_allowed_extension(user_stats, "c");
        add_allowed_extension(user_stats, "cc");
        add_allowed_extension(user_stats, "cpp");
        add_allowed_extension(user_stats, "cxx");
        return true;
    }
    if (type == "cpp" || type == "c++") {
        add_allowed_extension(user_stats, "h");
        add_allowed_extension(user_stats, "hh");
        add_allowed_extension(user_stats, "hpp");
        add_allowed_extension(user_stats, "hxx");
        add_allowed_extension(user_stats, "c");
        add_allowed_extension(user_stats, "cc");
        add_allowed_extension(user_stats, "cpp");
        add_allowed_extension(user_stats, "cxx");
        return true;
    }

    return false;
}

bool extension_filter_allows(std::string_view path, const UserOptions& user_stats)
{
    if (user_stats.allowed_extensions.empty()) {
        return true;
    }

    const size_t slash_pos = path.find_last_of('/');
    if (slash_pos != std::string_view::npos) {
        path.remove_prefix(slash_pos + 1);
    }

    const size_t dot_pos = path.find_last_of('.');
    if (dot_pos == std::string_view::npos || dot_pos == 0) {
        return false;
    }

    const std::string_view ext(path.data() + dot_pos, path.size() - dot_pos);
    for (const auto& allowed : user_stats.allowed_extensions) {
        if (ext.size() != allowed.size()) {
            continue;
        }

        bool matches = true;
        for (size_t i = 0; i < ext.size(); ++i) {
            if (lower_ascii(ext[i]) != allowed[i]) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }

    return false;
}

bool glob_matches(std::string_view pattern, std::string_view path)
{
    const size_t rows = pattern.size() + 1;
    const size_t cols = path.size() + 1;
    std::vector<int8_t> memo(rows * cols, -1);

    auto memo_at = [&](size_t pattern_pos, size_t path_pos) -> int8_t& {
        return memo[(pattern_pos * cols) + path_pos];
    };

    std::function<bool(size_t, size_t)> match_at = [&](size_t pattern_pos, size_t path_pos) -> bool {
        int8_t& cached = memo_at(pattern_pos, path_pos);
        if (cached != -1) {
            return cached == 1;
        }

        bool matched = false;
        if (pattern_pos == pattern.size()) {
            matched = path_pos == path.size();
        } else if (pattern_pos + 2 < pattern.size() &&
                   pattern[pattern_pos] == '*' &&
                   pattern[pattern_pos + 1] == '*' &&
                   pattern[pattern_pos + 2] == '/') {
            matched = match_at(pattern_pos + 3, path_pos);
            for (size_t next = path_pos; !matched && next < path.size(); ++next) {
                if (path[next] == '/') {
                    matched = match_at(pattern_pos + 3, next + 1);
                }
            }
        } else if (pattern_pos + 1 < pattern.size() &&
                   pattern[pattern_pos] == '*' &&
                   pattern[pattern_pos + 1] == '*') {
            for (size_t next = path_pos; !matched && next <= path.size(); ++next) {
                matched = match_at(pattern_pos + 2, next);
            }
        } else if (pattern[pattern_pos] == '*') {
            matched = match_at(pattern_pos + 1, path_pos);
            for (size_t next = path_pos; !matched && next < path.size() && path[next] != '/'; ++next) {
                matched = match_at(pattern_pos + 1, next + 1);
            }
        } else if (pattern[pattern_pos] == '?') {
            matched = path_pos < path.size() &&
                path[path_pos] != '/' &&
                match_at(pattern_pos + 1, path_pos + 1);
        } else {
            matched = path_pos < path.size() &&
                pattern[pattern_pos] == path[path_pos] &&
                match_at(pattern_pos + 1, path_pos + 1);
        }

        cached = matched ? 1 : 0;
        return matched;
    };

    return match_at(0, 0);
}

bool glob_matches_path(std::string_view pattern, std::string_view path)
{
    if (pattern.find('/') == std::string_view::npos) {
        const size_t slash_pos = path.find_last_of('/');
        if (slash_pos != std::string_view::npos) {
            path.remove_prefix(slash_pos + 1);
        }
        return glob_matches(pattern, path);
    }

    if (glob_matches(pattern, path)) {
        return true;
    }

    for (size_t pos = path.find('/'); pos != std::string_view::npos; pos = path.find('/', pos + 1)) {
        if (pos + 1 < path.size() && glob_matches(pattern, path.substr(pos + 1))) {
            return true;
        }
    }

    return false;
}

bool path_filter_allows(std::string_view path, const UserOptions& user_stats)
{
    if (!extension_filter_allows(path, user_stats)) {
        return false;
    }

    if (exclude_glob_matches_path(path, user_stats)) {
        return false;
    }

    if (user_stats.include_globs.empty()) {
        return true;
    }

    for (const auto& glob : user_stats.include_globs) {
        if (glob_matches_path(glob, path)) {
            return true;
        }
    }

    return false;
}

bool exclude_glob_matches_path(std::string_view path, const UserOptions& user_stats)
{
    for (const auto& glob : user_stats.exclude_globs) {
        if (glob_matches_path(glob, path)) {
            return true;
        }
    }

    return false;
}

bool exclude_glob_matches_directory(std::string_view path, const UserOptions& user_stats)
{
    if (user_stats.exclude_globs.empty()) {
        return false;
    }
    if (exclude_glob_matches_path(path, user_stats)) {
        return true;
    }

    std::string path_with_slash(path);
    path_with_slash.push_back('/');
    return exclude_glob_matches_path(path_with_slash, user_stats);
}

bool is_hex_digit(char ch)
{
    return (ch >= '0' && ch <= '9') ||
        (ch >= 'a' && ch <= 'f') ||
        (ch >= 'A' && ch <= 'F');
}

unsigned char hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return static_cast<unsigned char>(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return static_cast<unsigned char>(ch - 'a' + 10);
    }
    return static_cast<unsigned char>(ch - 'A' + 10);
}

std::string decode_pattern_escapes(const std::string& pattern)
{
    std::string decoded;
    decoded.reserve(pattern.size());

    for (size_t i = 0; i < pattern.size(); ++i) {
        const char ch = pattern[i];
        if (ch != '\\' || i + 1 >= pattern.size()) {
            decoded.push_back(ch);
            continue;
        }

        const char escaped = pattern[++i];
        switch (escaped) {
            case 'n':
                decoded.push_back('\n');
                break;
            case 'r':
                decoded.push_back('\r');
                break;
            case 't':
                decoded.push_back('\t');
                break;
            case '0':
                decoded.push_back('\0');
                break;
            case '\\':
                decoded.push_back('\\');
                break;
            case 'x':
                if (i + 2 < pattern.size() &&
                    is_hex_digit(pattern[i + 1]) &&
                    is_hex_digit(pattern[i + 2])) {
                    decoded.push_back(static_cast<char>(
                        (hex_value(pattern[i + 1]) << 4) | hex_value(pattern[i + 2])
                    ));
                    i += 2;
                } else {
                    decoded.push_back('x');
                }
                break;
            default:
                decoded.push_back(escaped);
                break;
        }
    }

    return decoded;
}

void printHelp(char* file_name)
{
    auto print_option = [](const char* option, const char* description) {
        cout << "\t" << FILE_ORANGE << option << RESET << "\t" << description << "\n";
    };

    cout << FILE_ORANGE << "Printing help for program: " << RESET << file_name << "\n";
    print_option("-h", "Prints this marvalous help test");
    print_option("-v, --invert-match", "Prints lines that do not contain the pattern");
    print_option("-i, --ignore-case", "Matches ASCII letters case-insensitively");
    print_option("--verbose", "Enables verbose output ");
    print_option("-r", "Recursively search all dirs below dir provided");
    print_option("-p, --pretty", "Compatibility alias; colors are enabled by default");
    print_option("-t, --theme THEME", "Select color theme: blue, red, green, purple,");
    cout << "\t\tcyan, yellow, orange, pink, mono, bright,\n";
    cout << "\t\tgruvbox, nord, dracula, nebula\n";
    print_option("-c, --count", "Prints matching line counts instead of normal matches");
    print_option("-q, --quiet", "Prints nothing, only returns match status");
    print_option("-o, --only-matching", "Prints only matching text, one occurrence per line");
    print_option("--type TYPE", "Only searches files in a named type: header, source, cpp");
    print_option("--ext EXT[,EXT...]", "Only searches files with matching extensions");
    print_option("--glob GLOB", "Only searches paths matching GLOB");
    print_option("--exclude-glob GLOB", "Skips paths matching GLOB");
    print_option("--heading", "Groups line/source output under each matching file path");
    print_option("--files", "Lists files mgrep would search, without requiring a pattern");
    print_option("--files-from FILE", "Reads newline-delimited input file paths from FILE");
    print_option("--files-from0 FILE, --null-files-from FILE", "Reads NUL-delimited input file paths from FILE");
    print_option("--no-color", "Disable ANSI color output");
    print_option("--literal", "Treat backslashes in the pattern literally");
    print_option("--colors COMPONENT:ATTR:VALUE", "Override colors. Components: path, file, line, match, source");
    cout << "\t\tAttrs: fg, bg, style. Example: --colors match:fg:magenta\n";
    print_option("-n", "Prints an additional newline between pattern finds");
    print_option("-l", "Prints line number in file pattern is found in");
    print_option("-s", "Prints line of source code the pattern was found in");
    print_option("-a", "Searches all files.");
    cout << "\t\tBy default, mgrep skips hidden dirs, common build/cache dirs,\n";
    cout << "\t\tbinary-looking files, archives, media, object files, and files\n";
    cout << "\t\twithout extensions. Use -a to search everything.\n";

    cout << "\nmgrep is a groundbreaking new program brought to you by MNU\n";
    cout << "\tUsage:\tmgrep -[OPTIONS] \"pattern\" root_dir\n";
    cout << "\tgrep -r \"main\" .\n";
    cout << "Exactly like grep, but better because I made it.\n";
}

ParseResult parse_user_options(int argc, char* argv[], UserOptions& user_stats)
{
    std::vector<std::string> original_args;
    original_args.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
    for (int i = 1; i < argc; ++i) {
        original_args.emplace_back(argv[i]);
    }

    std::vector<std::string> color_overrides;
    bool literal_pattern = false;
    int opt = 0;
    static option long_options[] = {
        {"colors", required_argument, nullptr, COLORS_OPTION},
        {"count", no_argument, nullptr, COUNT_OPTION},
        {"pretty", no_argument, nullptr, PRETTY_OPTION},
        {"theme", required_argument, nullptr, THEME_OPTION},
        {"no-color", no_argument, nullptr, NO_COLOR_OPTION},
        {"quiet", no_argument, nullptr, QUIET_OPTION},
        {"files-from", required_argument, nullptr, FILES_FROM_OPTION},
        {"files-from0", required_argument, nullptr, FILES_FROM0_OPTION},
        {"null-files-from", required_argument, nullptr, FILES_FROM0_OPTION},
        {"only-matching", no_argument, nullptr, ONLY_MATCHING_OPTION},
        {"invert-match", no_argument, nullptr, INVERT_MATCH_OPTION},
        {"ignore-case", no_argument, nullptr, IGNORE_CASE_OPTION},
        {"literal", no_argument, nullptr, LITERAL_OPTION},
        {"type", required_argument, nullptr, TYPE_OPTION},
        {"ext", required_argument, nullptr, EXT_OPTION},
        {"glob", required_argument, nullptr, GLOB_OPTION},
        {"exclude-glob", required_argument, nullptr, EXCLUDE_GLOB_OPTION},
        {"heading", no_argument, nullptr, HEADING_OPTION},
        {"files", no_argument, nullptr, FILES_OPTION},
        {"verbose", no_argument, nullptr, VERBOSE_OPTION},
        {nullptr, 0, nullptr, 0}
    };

    while ((opt = getopt_long(argc, argv, OPTIONS, long_options, nullptr)) != -1) {
        switch(opt) {
            case 'v':
                user_stats.invert_match = true;
                break;
            case 'i':
                user_stats.ignore_case = true;
                break;
            case 'h':
                printHelp(argv[0]);
                return {false, MGREP_EXIT_MATCH_FOUND, optind};
            case 'r':
                user_stats.recursive_mode = true;
                break;
            case 'c':
                user_stats.count_print = true;
                break;
            case 'p':
                user_stats.cool_colors = true;
                break;
            case 'n':
                user_stats.add_newline = true;
                break;
            case 'l':
                user_stats.line_number_print = true;
                break;
            case 's':
                user_stats.source_print = true;
                break;
            case 'a':
                user_stats.all_files = true;
                break;
            case 'q':
                user_stats.quiet = true;
                break;
            case 'o':
                user_stats.only_matching = true;
                break;
            case 't':
                if (!parse_color_theme(optarg, user_stats.colors)) {
                    std::cerr << "ERROR: invalid theme: " << optarg << "\n";
                    return {false, MGREP_EXIT_ERROR, optind};
                }
                user_stats.cool_colors = true;
                break;
            case 'B':
                if (!parse_nonnegative_option(optarg, user_stats.print_before_source)) {
                    std::cerr << "ERROR: invalid before-context value: " << optarg << "\n";
                    return {false, MGREP_EXIT_ERROR, optind};
                }
                break;
            case 'A':
                if (!parse_nonnegative_option(optarg, user_stats.print_after_source)) {
                    std::cerr << "ERROR: invalid after-context value: " << optarg << "\n";
                    return {false, MGREP_EXIT_ERROR, optind};
                }
                break;
            case 'm':
                if (!parse_max_lines_option(optarg, user_stats.max_lines)) {
                    std::cerr << "ERROR: invalid max-lines value: " << optarg << "\n";
                    return {false, MGREP_EXIT_ERROR, optind};
                }
                break;
            case COLORS_OPTION:
                user_stats.cool_colors = true;
                color_overrides.emplace_back(optarg);
                break;
            case COUNT_OPTION:
                user_stats.count_print = true;
                break;
            case PRETTY_OPTION:
                user_stats.cool_colors = true;
                break;
            case THEME_OPTION:
                if (!parse_color_theme(optarg, user_stats.colors)) {
                    std::cerr << "ERROR: invalid theme: " << optarg << "\n";
                    return {false, MGREP_EXIT_ERROR, optind};
                }
                user_stats.cool_colors = true;
                break;
            case NO_COLOR_OPTION:
                user_stats.cool_colors = false;
                break;
            case QUIET_OPTION:
                user_stats.quiet = true;
                break;
            case FILES_FROM_OPTION:
                user_stats.files_from.emplace_back(optarg);
                user_stats.file_lists.emplace_back(optarg, '\n');
                break;
            case FILES_FROM0_OPTION:
                user_stats.files_from0.emplace_back(optarg);
                user_stats.file_lists.emplace_back(optarg, '\0');
                break;
            case ONLY_MATCHING_OPTION:
                user_stats.only_matching = true;
                break;
            case INVERT_MATCH_OPTION:
                user_stats.invert_match = true;
                break;
            case IGNORE_CASE_OPTION:
                user_stats.ignore_case = true;
                break;
            case LITERAL_OPTION:
                literal_pattern = true;
                break;
            case TYPE_OPTION:
                if (!parse_file_type(optarg, user_stats)) {
                    std::cerr << "ERROR: invalid type: " << optarg << "\n";
                    return {false, MGREP_EXIT_ERROR, optind};
                }
                break;
            case EXT_OPTION:
                if (!parse_extension_list(optarg, user_stats)) {
                    std::cerr << "ERROR: invalid extension list: " << optarg << "\n";
                    return {false, MGREP_EXIT_ERROR, optind};
                }
                break;
            case GLOB_OPTION:
                if (optarg[0] == '\0') {
                    std::cerr << "ERROR: invalid glob: " << optarg << "\n";
                    return {false, MGREP_EXIT_ERROR, optind};
                }
                user_stats.include_globs.emplace_back(optarg);
                break;
            case EXCLUDE_GLOB_OPTION:
                if (optarg[0] == '\0') {
                    std::cerr << "ERROR: invalid exclude glob: " << optarg << "\n";
                    return {false, MGREP_EXIT_ERROR, optind};
                }
                user_stats.exclude_globs.emplace_back(optarg);
                break;
            case HEADING_OPTION:
                user_stats.heading = true;
                break;
            case FILES_OPTION:
                user_stats.list_files = true;
                user_stats.recursive_mode = true;
                break;
            case VERBOSE_OPTION:
                user_stats.is_verbose = true;
                break;
            default:
                return {false, MGREP_EXIT_ERROR, optind};
        }
    }

    for (const auto& color_override : color_overrides) {
        if (!parse_color_override(color_override, user_stats.colors)) {
            std::cerr << "ERROR: invalid --colors value: " << color_override << "\n";
            return {false, MGREP_EXIT_ERROR, optind};
        }
    }
    if (user_stats.only_matching && user_stats.invert_match) {
        std::cerr << "ERROR: --only-matching cannot be used with --invert-match\n";
        return {false, MGREP_EXIT_ERROR, optind};
    }
    if (user_stats.list_files) {
        user_stats.pattern.clear();
        user_stats.folded_pattern.clear();
    } else if (argv[optind]) {
        user_stats.pattern = literal_pattern
            ? std::string(argv[optind])
            : decode_pattern_escapes(argv[optind]);
        if (user_stats.ignore_case) {
            user_stats.folded_pattern = fold_ascii_string(user_stats.pattern);
        }
        ++optind;
    } else {
        std::cerr << "ERROR: missing pattern\n";
        return {false, MGREP_EXIT_ERROR, optind};
    }

    record_input_operands(original_args, user_stats);
    if (user_stats.list_files && user_stats.input_operands.empty()) {
        user_stats.input_operands.push_back({InputOperand::Kind::Path, ".", '\n'});
    }

    return {true, MGREP_EXIT_MATCH_FOUND, optind};
}
