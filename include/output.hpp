#pragma once

#include "colors.hpp"

#include <cstddef>
#include <string>

size_t filename_pos(const std::string& path);
void append_colored_path(
    std::string& output,
    const std::string& path,
    size_t name_pos,
    const ColorTheme& colors
);
void append_plain_path(std::string& output, const std::string& path, size_t name_pos);
void append_highlighted_source(
    std::string& output,
    const char* line_data,
    size_t line_len,
    const char* hit,
    size_t pattern_len,
    const ColorTheme& colors
);
void append_one_line_source(
    std::string& output,
    const std::string* path,
    size_t line_num,
    bool print_line_number,
    const char* line_data,
    size_t line_len,
    const char* hit,
    size_t pattern_len,
    const ColorTheme& colors,
    bool cool_colors
);
