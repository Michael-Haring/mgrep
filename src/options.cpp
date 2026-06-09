#include "options.hpp"

#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <vector>

constexpr char* OPTIONS = (char*)"vhrcpnlsaqoB:A:m:";
constexpr int COLORS_OPTION = 1000;
constexpr int COUNT_OPTION = 1001;
constexpr int PRETTY_OPTION = 1002;
constexpr int QUIET_OPTION = 1003;
constexpr int FILES_FROM_OPTION = 1004;
constexpr int FILES_FROM0_OPTION = 1005;
constexpr int ONLY_MATCHING_OPTION = 1006;
constexpr int INVERT_MATCH_OPTION = 1007;
constexpr int VERBOSE_OPTION = 1008;

using std::cout;

void printHelp(char* file_name)
{
    auto print_option = [](const char* option, const char* description) {
        cout << "\t" << FILE_ORANGE << option << RESET << "\t" << description << "\n";
    };

    cout << FILE_ORANGE << "Printing help for program: " << RESET << file_name << "\n";
    print_option("-h", "Prints this marvalous help test");
    print_option("-v, --invert-match", "Prints lines that do not contain the pattern");
    print_option("--verbose", "Enables verbose output ");
    print_option("-r", "Recursively search all dirs below dir provided");
    print_option("-p, --pretty", "Enables colorful output. Optional presets: blue, red, green, purple,");
    cout << "\t\tcyan, yellow, orange, pink, mono, bright\n";
    print_option("-c, --count", "Prints matching line counts instead of normal matches");
    print_option("-q, --quiet", "Prints nothing, only returns match status");
    print_option("-o, --only-matching", "Prints only matching text, one occurrence per line");
    print_option("--files-from FILE", "Reads newline-delimited input file paths from FILE");
    print_option("--files-from0 FILE, --null-files-from FILE", "Reads NUL-delimited input file paths from FILE");
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
    std::vector<std::string> color_overrides;
    int opt = 0;
    static option long_options[] = {
        {"colors", required_argument, nullptr, COLORS_OPTION},
        {"count", no_argument, nullptr, COUNT_OPTION},
        {"pretty", no_argument, nullptr, PRETTY_OPTION},
        {"quiet", no_argument, nullptr, QUIET_OPTION},
        {"files-from", required_argument, nullptr, FILES_FROM_OPTION},
        {"files-from0", required_argument, nullptr, FILES_FROM0_OPTION},
        {"null-files-from", required_argument, nullptr, FILES_FROM0_OPTION},
        {"only-matching", no_argument, nullptr, ONLY_MATCHING_OPTION},
        {"invert-match", no_argument, nullptr, INVERT_MATCH_OPTION},
        {"verbose", no_argument, nullptr, VERBOSE_OPTION},
        {nullptr, 0, nullptr, 0}
    };

    while ((opt = getopt_long(argc, argv, OPTIONS, long_options, nullptr)) != -1) {
        switch(opt) {
            case 'v':
                user_stats.invert_match = true;
                break;
            case 'h':
                printHelp(argv[0]);
                return {true, MGREP_EXIT_MATCH_FOUND, optind};
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
            case 'B':
                user_stats.print_before_source = atoi(optarg);
                break;
            case 'A':
                user_stats.print_after_source = atoi(optarg);
                break;
            case 'm':
                user_stats.max_lines = atoi(optarg);
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
            case QUIET_OPTION:
                user_stats.quiet = true;
                break;
            case FILES_FROM_OPTION:
                user_stats.files_from.emplace_back(optarg);
                break;
            case FILES_FROM0_OPTION:
                user_stats.files_from0.emplace_back(optarg);
                break;
            case ONLY_MATCHING_OPTION:
                user_stats.only_matching = true;
                break;
            case INVERT_MATCH_OPTION:
                user_stats.invert_match = true;
                break;
            case VERBOSE_OPTION:
                user_stats.is_verbose = true;
                break;
            default:
                return {false, MGREP_EXIT_ERROR, optind};
        }
    }

    if (argv[optind]) {
        if (user_stats.cool_colors && parse_color_theme(argv[optind], user_stats.colors)) {
            ++optind;
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
    if (argv[optind]) {
        user_stats.pattern = argv[optind];
        ++optind;
    } else {
        std::cerr << "ERROR: missing pattern\n";
        return {false, MGREP_EXIT_ERROR, optind};
    }

    return {true, MGREP_EXIT_MATCH_FOUND, optind};
}
