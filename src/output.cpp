#include "output.hpp"

size_t filename_pos(const std::string& path)
{
    const size_t slash_pos = path.find_last_of('/');
    return slash_pos == std::string::npos ? 0 : slash_pos + 1;
}

void append_colored_path(
    std::string& output,
    const std::string& path,
    size_t name_pos,
    const ColorTheme& colors
)
{
    if (name_pos > 0) {
        output.append(colors.path);
        output.append(path.data(), name_pos);
        output.append(RESET);
        output.push_back('\t');
    }
    output.append(colors.file);
    output.append(path.data() + name_pos, path.size() - name_pos);
    output.append(RESET);
}

void append_plain_path(std::string& output, const std::string& path, size_t name_pos)
{
    if (name_pos > 0) {
        output.append(path.data(), name_pos);
        output.push_back('\t');
    }
    output.append(path.data() + name_pos, path.size() - name_pos);
}

void append_highlighted_source(
    std::string& output,
    const char* line_data,
    size_t line_len,
    const char* hit,
    size_t pattern_len,
    const ColorTheme& colors
) {
    if (hit == nullptr || hit < line_data || hit + pattern_len > line_data + line_len) {
        if (!colors.source.empty()) {
            output.append(colors.source);
        }
        output.append(line_data, line_len);
        if (!colors.source.empty()) {
            output.append(RESET);
        }
        return;
    }

    if (!colors.source.empty()) {
        output.append(colors.source);
    }
    output.append(line_data, hit - line_data);
    output.append(colors.match);
    output.append(hit, pattern_len);
    output.append(RESET);
    if (!colors.source.empty()) {
        output.append(colors.source);
    }
    output.append(hit + pattern_len, line_data + line_len - hit - pattern_len);
    if (!colors.source.empty()) {
        output.append(RESET);
    }
}
