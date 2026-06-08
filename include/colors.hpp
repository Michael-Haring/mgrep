#pragma once

#include <string>
#include <string_view>

inline constexpr const char* PATH_GREY = "\033[38;5;244m";
inline constexpr const char* FILE_BLUE = "\033[38;5;75m";
inline constexpr const char* FILE_RED = "\033[38;5;203m";
inline constexpr const char* FILE_GREEN = "\033[38;5;114m";
inline constexpr const char* FILE_PURPLE = "\033[38;5;141m";
inline constexpr const char* FILE_CYAN = "\033[38;5;87m";
inline constexpr const char* FILE_YELLOW = "\033[38;5;222m";
inline constexpr const char* FILE_ORANGE = "\033[38;5;215m";
inline constexpr const char* FILE_PINK = "\033[38;5;213m";
inline constexpr const char* FILE_WHITE = "\033[38;5;231m";
inline constexpr const char* MATCH_BLUE = "\033[38;5;81m";
inline constexpr const char* MATCH_RED = "\033[38;5;210m";
inline constexpr const char* MATCH_GREEN = "\033[38;5;120m";
inline constexpr const char* MATCH_PURPLE = "\033[38;5;177m";
inline constexpr const char* MATCH_CYAN = "\033[38;5;123m";
inline constexpr const char* MATCH_YELLOW = "\033[38;5;226m";
inline constexpr const char* MATCH_ORANGE = "\033[38;5;208m";
inline constexpr const char* MATCH_PINK = "\033[38;5;218m";
inline constexpr const char* MATCH_WHITE = "\033[1m\033[38;5;231m";
inline constexpr const char* LINE_TEAL = "\033[38;5;37m";
inline constexpr const char* LINE_RED = "\033[38;5;167m";
inline constexpr const char* LINE_GREEN = "\033[38;5;72m";
inline constexpr const char* LINE_PURPLE = "\033[38;5;98m";
inline constexpr const char* LINE_CYAN = "\033[38;5;44m";
inline constexpr const char* LINE_YELLOW = "\033[38;5;178m";
inline constexpr const char* LINE_ORANGE = "\033[38;5;172m";
inline constexpr const char* LINE_PINK = "\033[38;5;176m";
inline constexpr const char* LINE_WHITE = "\033[38;5;250m";
inline constexpr const char* RESET = "\033[0m";

struct ColorTheme {
    std::string path = PATH_GREY;
    std::string file = FILE_BLUE;
    std::string line = LINE_TEAL;
    std::string match = MATCH_BLUE;
    std::string source = "";
};

bool parse_color_theme(std::string_view name, ColorTheme& colors);
bool parse_color_override(std::string_view spec, ColorTheme& colors);
