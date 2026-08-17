#include "colors.hpp"

#include <iostream>

namespace {

constexpr std::string_view THEME_NAMES[] = {
    "blue", "red", "green", "purple", "cyan", "yellow", "orange", "pink",
    "mono", "bright", "gruvbox", "nord", "dracula", "nebula"
};

void print_preview_line(
    const ColorTheme& colors,
    bool cool_colors,
    std::string_view line_number,
    std::string_view before_match,
    std::string_view match,
    std::string_view after_match
)
{
    if (!cool_colors) {
        std::cout << "  " << line_number << ": " << before_match << match << after_match << '\n';
        return;
    }

    std::cout << "  " << colors.line << line_number << ":" << RESET << ' '
              << colors.source << before_match
              << colors.match << match << RESET
              << colors.source << after_match << RESET << '\n';
}

} // namespace

bool parse_color_theme(std::string_view name, ColorTheme& colors)
{
    if (name == "blue") {
        colors = {PATH_GREY, FILE_BLUE, LINE_TEAL, MATCH_BLUE, SOURCE_GREY};
        return true;
    }
    if (name == "red") {
        colors = {PATH_GREY, FILE_RED, LINE_RED, MATCH_RED, SOURCE_GREY};
        return true;
    }
    if (name == "green") {
        colors = {PATH_GREY, FILE_GREEN, LINE_GREEN, MATCH_GREEN, SOURCE_GREY};
        return true;
    }
    if (name == "purple") {
        colors = {PATH_GREY, FILE_PURPLE, LINE_PURPLE, MATCH_PURPLE, SOURCE_GREY};
        return true;
    }
    if (name == "cyan") {
        colors = {PATH_GREY, FILE_CYAN, LINE_CYAN, MATCH_CYAN, SOURCE_GREY};
        return true;
    }
    if (name == "yellow") {
        colors = {PATH_GREY, FILE_YELLOW, LINE_YELLOW, MATCH_YELLOW, SOURCE_GREY};
        return true;
    }
    if (name == "orange") {
        colors = {PATH_GREY, FILE_ORANGE, LINE_ORANGE, MATCH_ORANGE, SOURCE_GREY};
        return true;
    }
    if (name == "pink") {
        colors = {PATH_GREY, FILE_PINK, LINE_PINK, MATCH_PINK, SOURCE_GREY};
        return true;
    }
    if (name == "mono") {
        colors = {PATH_GREY, FILE_WHITE, LINE_WHITE, MATCH_WHITE, SOURCE_GREY};
        return true;
    }
    if (name == "bright") {
        colors = {LINE_WHITE, FILE_CYAN, FILE_YELLOW, MATCH_PINK, SOURCE_GREY};
        return true;
    }
    if (name == "gruvbox") {
        colors = {PATH_GREY, FILE_GRUVBOX, LINE_GRUVBOX, MATCH_GRUVBOX, SOURCE_GREY};
        return true;
    }
    if (name == "nord") {
        colors = {PATH_GREY, FILE_NORD, LINE_NORD, MATCH_NORD, SOURCE_GREY};
        return true;
    }
    if (name == "dracula") {
        colors = {PATH_GREY, FILE_DRACULA, LINE_DRACULA, MATCH_DRACULA, SOURCE_GREY};
        return true;
    }
    if (name == "nebula") {
        colors = {PATH_GREY, FILE_NEBULA, LINE_NEBULA, MATCH_NEBULA, SOURCE_GREY};
        return true;
    }

    return false;
}

void print_theme_previews(bool cool_colors)
{
    for (std::string_view name : THEME_NAMES) {
        ColorTheme colors;
        parse_color_theme(name, colors);

        if (cool_colors) {
            std::cout << colors.file << name << RESET << '\n'
                      << "  " << colors.path << "src/" << RESET
                      << colors.file << "search.cpp" << RESET << '\n';
        } else {
            std::cout << name << "\n  src/search.cpp\n";
        }

        print_preview_line(colors, cool_colors, "42", "if (", "pattern_matches", "(line)) {");
        print_preview_line(colors, cool_colors, "87", "    ", "matches", " += search_file(path);");
        std::cout << '\n';
    }
}

int parse_color_value(std::string_view value)
{
    if (value == "black") {
        return 16;
    }
    if (value == "red") {
        return 196;
    }
    if (value == "green") {
        return 46;
    }
    if (value == "yellow") {
        return 226;
    }
    if (value == "blue") {
        return 21;
    }
    if (value == "magenta" || value == "purple") {
        return 201;
    }
    if (value == "cyan") {
        return 51;
    }
    if (value == "white") {
        return 231;
    }
    if (value == "gray" || value == "grey") {
        return 244;
    }

    if (value.empty()) {
        return -1;
    }

    int color = 0;
    for (char ch : value) {
        if (ch < '0' || ch > '9') {
            return -1;
        }
        color = (color * 10) + (ch - '0');
        if (color > 255) {
            return -1;
        }
    }

    return color;
}

void remove_color_attr(std::string& target, std::string_view attr)
{
    const std::string_view prefix = attr == "fg" ? "\033[38;5;" : "\033[48;5;";
    size_t pos = 0;

    while ((pos = target.find(prefix, pos)) != std::string::npos) {
        const size_t end = target.find('m', pos + prefix.size());
        if (end == std::string::npos) {
            return;
        }

        target.erase(pos, end - pos + 1);
    }
}

bool append_color_code(std::string& target, std::string_view attr, std::string_view value)
{
    if (attr == "fg" || attr == "bg") {
        const int color = parse_color_value(value);
        if (color < 0) {
            return false;
        }

        remove_color_attr(target, attr);
        target.append(attr == "fg" ? "\033[38;5;" : "\033[48;5;");
        target.append(std::to_string(color));
        target.push_back('m');
        return true;
    }

    if (attr != "style") {
        return false;
    }

    if (value == "bold") {
        target.append("\033[1m");
        return true;
    }
    if (value == "dim") {
        target.append("\033[2m");
        return true;
    }
    if (value == "italic") {
        target.append("\033[3m");
        return true;
    }
    if (value == "underline") {
        target.append("\033[4m");
        return true;
    }
    if (value == "reverse") {
        target.append("\033[7m");
        return true;
    }

    return false;
}

bool parse_color_override(std::string_view spec, ColorTheme& colors)
{
    const size_t component_end = spec.find(':');
    if (component_end == std::string_view::npos) {
        return false;
    }

    const size_t attr_end = spec.find(':', component_end + 1);
    if (attr_end == std::string_view::npos) {
        return false;
    }

    const std::string_view component = spec.substr(0, component_end);
    const std::string_view attr = spec.substr(component_end + 1, attr_end - component_end - 1);
    const std::string_view value = spec.substr(attr_end + 1);

    if (component == "path") {
        return append_color_code(colors.path, attr, value);
    }
    if (component == "file") {
        return append_color_code(colors.file, attr, value);
    }
    if (component == "line") {
        return append_color_code(colors.line, attr, value);
    }
    if (component == "match") {
        return append_color_code(colors.match, attr, value);
    }
    if (component == "source") {
        return append_color_code(colors.source, attr, value);
    }

    return false;
}
