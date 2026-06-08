#include "options.hpp"

#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <vector>

constexpr char* OPTIONS = (char*)"vhrcpnlsaB:A:m:";
constexpr int COLORS_OPTION = 1000;
constexpr int COUNT_OPTION = 1001;
constexpr int PRETTY_OPTION = 1002;

using std::cout;

void printHelp(char* file_name)
{
    cout << FILE_BLUE << "Printing help for program: " << file_name << "\n";
    cout << "\t-h\tPrints this marvalous help test\n";
    cout << "\t-v\tEnables verbose output \n";
    cout << "\t-r\tRecursively search all dirs below dir provided\n";
    cout << "\t-p, --pretty\tEnables colorful output. Optional presets: blue, red, green, purple,\n";
    cout << "\t\tcyan, yellow, orange, pink, mono, bright\n";
    cout << "\t-c, --count\tPrints matching line counts instead of normal matches\n";
    cout << "\t--colors COMPONENT:ATTR:VALUE\tOverride colors. Components: path, file, line, match, source\n";
    cout << "\t\tAttrs: fg, bg, style. Example: --colors match:fg:magenta\n";
    cout << "\t-n\tPrints an additional newline between pattern finds\n";
    cout << "\t-l\tPrints line number in file pattern is found in\n";
    cout << "\t-s\tPrints line of source code the pattern was found in\n";
    cout << "\t-a\tSearches all files.\n";
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
        {nullptr, 0, nullptr, 0}
    };

    while ((opt = getopt_long(argc, argv, OPTIONS, long_options, nullptr)) != -1) {
        switch(opt) {
            case 'v':
                user_stats.is_verbose = true;
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
    if (argv[optind]) {
        user_stats.pattern = argv[optind];
        ++optind;
    } else {
        std::cerr << "ERROR: missing pattern\n";
        return {false, MGREP_EXIT_ERROR, optind};
    }

    return {true, MGREP_EXIT_MATCH_FOUND, optind};
}
