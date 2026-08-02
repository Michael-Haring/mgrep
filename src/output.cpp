#include "output.hpp"

#include <algorithm>

namespace {

constexpr size_t ONE_LINE_WIDTH = 100;
constexpr const char* ELLIPSIS = "\xE2\x80\xA6";

size_t decimal_digits(size_t value)
{
    size_t digits = 1;
    while (value >= 10) {
        value /= 10;
        ++digits;
    }
    return digits;
}

size_t append_compact_path(
    std::string& output,
    const std::string& path,
    size_t name_pos,
    size_t max_width,
    const ColorTheme& colors,
    bool cool_colors
)
{
    const bool clipped = path.size() > max_width;
    const size_t suffix_len = clipped && max_width > 1 ? max_width - 1 : max_width;
    const size_t suffix_pos = path.size() - suffix_len;

    if (!cool_colors) {
        if (clipped && max_width > 0) {
            output.append(ELLIPSIS);
        }
        output.append(path.data() + suffix_pos, suffix_len);
        return clipped && max_width > 0 ? suffix_len + 1 : suffix_len;
    }

    if (suffix_pos < name_pos) {
        output.append(colors.path);
        if (clipped && max_width > 0) {
            output.append(ELLIPSIS);
        }
        output.append(path.data() + suffix_pos, name_pos - suffix_pos);
        output.append(RESET);
        output.append(colors.file);
        output.append(path.data() + name_pos, path.size() - name_pos);
    } else {
        output.append(colors.file);
        if (clipped && max_width > 0) {
            output.append(ELLIPSIS);
        }
        output.append(path.data() + suffix_pos, suffix_len);
    }
    output.append(RESET);
    return clipped && max_width > 0 ? suffix_len + 1 : suffix_len;
}

} // namespace

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

void append_one_line_source(
    std::string& output,
    const std::string* path,
    size_t path_name_pos,
    size_t line_num,
    bool print_line_number,
    const char* line_data,
    size_t line_len,
    const char* hit,
    size_t pattern_len,
    const ColorTheme& colors,
    bool cool_colors
)
{
    const size_t line_prefix_width = print_line_number ? decimal_digits(line_num) + 2 : 0;
    const size_t separator_width = path == nullptr ? 0 : 1;
    const size_t minimum_source_width = std::min(
        ONE_LINE_WIDTH,
        std::max<size_t>(20, std::min(pattern_len + 2, ONE_LINE_WIDTH))
    );
    const size_t fixed_width = line_prefix_width + separator_width;
    const size_t path_width = path == nullptr || fixed_width + minimum_source_width >= ONE_LINE_WIDTH
        ? 0
        : std::min(path->size(), ONE_LINE_WIDTH - fixed_width - minimum_source_width);

    size_t prefix_width = 0;
    if (path != nullptr) {
        prefix_width += append_compact_path(
            output, *path, path_name_pos, path_width, colors, cool_colors
        );
        output.push_back(' ');
        ++prefix_width;
    }
    if (print_line_number) {
        if (cool_colors) {
            output.append(colors.line);
        }
        output.append(std::to_string(line_num));
        output.append(": ");
        if (cool_colors) {
            output.append(RESET);
        }
        prefix_width += line_prefix_width;
    }

    const size_t source_width = prefix_width < ONE_LINE_WIDTH
        ? ONE_LINE_WIDTH - prefix_width
        : 0;
    if (source_width == 0 || line_len == 0) {
        return;
    }
    if (line_len <= source_width) {
        if (cool_colors) {
            append_highlighted_source(
                output, line_data, line_len, hit, pattern_len, colors
            );
        } else {
            output.append(line_data, line_len);
        }
        return;
    }

    const bool valid_hit = hit != nullptr && hit >= line_data &&
        hit + pattern_len <= line_data + line_len;
    size_t slice_start = 0;
    size_t slice_len = source_width > 1 ? source_width - 1 : 0;
    if (valid_hit) {
        slice_len = source_width > 2 ? source_width - 2 : source_width;
        const size_t hit_offset = static_cast<size_t>(hit - line_data);
        const size_t left_context = slice_len > pattern_len
            ? (slice_len - pattern_len) / 2
            : 0;
        slice_start = hit_offset > left_context ? hit_offset - left_context : 0;
        if (slice_start + slice_len > line_len) {
            slice_start = line_len - slice_len;
        }
    }

    const bool left_clipped = slice_start > 0;
    const size_t slice_end = std::min(line_len, slice_start + slice_len);
    if (left_clipped) {
        output.append(ELLIPSIS);
    }
    if (cool_colors) {
        append_highlighted_source(
            output,
            line_data + slice_start,
            slice_end - slice_start,
            valid_hit ? hit : nullptr,
            pattern_len,
            colors
        );
    } else {
        output.append(line_data + slice_start, slice_end - slice_start);
    }
    if (slice_end < line_len) {
        output.append(ELLIPSIS);
    }
}
