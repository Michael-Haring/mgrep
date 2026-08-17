#pragma once

#include <string>
#include <string_view>

inline constexpr const char* PATH_GREY = "\033[38;5;244m";
inline constexpr const char* FILE_BLUE = "\033[38;5;75m";
inline constexpr const char* FILE_RED = "\033[38;5;196m";
inline constexpr const char* FILE_GREEN = "\033[38;5;46m";
inline constexpr const char* FILE_PURPLE = "\033[38;5;141m";
inline constexpr const char* FILE_CYAN = "\033[38;5;87m";
inline constexpr const char* FILE_YELLOW = "\033[38;5;222m";
inline constexpr const char* FILE_ORANGE = "\033[38;5;215m";
inline constexpr const char* FILE_PINK = "\033[38;5;213m";
inline constexpr const char* FILE_WHITE = "\033[38;5;231m";
inline constexpr const char* MATCH_BLUE = "\033[38;5;81m";
inline constexpr const char* MATCH_RED = "\033[38;5;217m";
inline constexpr const char* MATCH_GREEN = "\033[38;5;154m";
inline constexpr const char* MATCH_PURPLE = "\033[38;5;177m";
inline constexpr const char* MATCH_CYAN = "\033[38;5;123m";
inline constexpr const char* MATCH_YELLOW = "\033[38;5;226m";
inline constexpr const char* MATCH_ORANGE = "\033[38;5;208m";
inline constexpr const char* MATCH_PINK = "\033[38;5;218m";
inline constexpr const char* MATCH_WHITE = "\033[1m\033[38;5;231m";
inline constexpr const char* LINE_TEAL = "\033[38;5;37m";
inline constexpr const char* LINE_RED = "\033[38;5;124m";
inline constexpr const char* LINE_GREEN = "\033[38;5;28m";
inline constexpr const char* LINE_PURPLE = "\033[38;5;98m";
inline constexpr const char* LINE_CYAN = "\033[38;5;44m";
inline constexpr const char* LINE_YELLOW = "\033[38;5;178m";
inline constexpr const char* LINE_ORANGE = "\033[38;5;172m";
inline constexpr const char* LINE_PINK = "\033[38;5;176m";
inline constexpr const char* LINE_WHITE = "\033[38;5;250m";
inline constexpr const char* SOURCE_GREY = "\033[38;5;248m";
inline constexpr const char* FILE_GRUVBOX = "\033[38;5;214m";
inline constexpr const char* LINE_GRUVBOX = "\033[38;5;108m";
inline constexpr const char* MATCH_GRUVBOX = "\033[38;5;208m";
inline constexpr const char* FILE_NORD = "\033[38;5;110m";
inline constexpr const char* LINE_NORD = "\033[38;5;67m";
inline constexpr const char* MATCH_NORD = "\033[38;5;153m";
inline constexpr const char* FILE_DRACULA = "\033[38;5;117m";
inline constexpr const char* LINE_DRACULA = "\033[38;5;141m";
inline constexpr const char* MATCH_DRACULA = "\033[38;5;212m";
inline constexpr const char* FILE_NEBULA = "\033[38;5;183m";
inline constexpr const char* LINE_NEBULA = "\033[38;5;73m";
inline constexpr const char* MATCH_NEBULA = "\033[38;5;159m";
inline constexpr const char* RESET = "\033[0m";

struct ColorTheme {
    std::string path = PATH_GREY;
    std::string file = FILE_BLUE;
    std::string line = LINE_TEAL;
    std::string match = MATCH_BLUE;
    std::string source = SOURCE_GREY;
};

bool parse_color_theme(std::string_view name, ColorTheme& colors);
bool parse_color_override(std::string_view spec, ColorTheme& colors);
void print_theme_previews(bool cool_colors);
