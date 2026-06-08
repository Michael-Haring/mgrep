#include "search.hpp"

#include "output.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

using std::cout;

size_t matches = 0;

size_t read_file_fast(const std::string& path, UserOptions& user_stats, std::string& output)
{
    constexpr size_t FAST_BUFFER_SIZE = 128 * 1024;

    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
        return 0;
    }

    const size_t output_start = output.size();
    size_t local_matches = 0;
    size_t cached_name_pos = std::string::npos;
    auto get_name_pos = [&]() {
        if (cached_name_pos == std::string::npos) {
            cached_name_pos = filename_pos(path);
        }
        return cached_name_pos;
    };

    const std::string& pattern = user_stats.pattern;
    const size_t pattern_len = pattern.size();

    const size_t overlap_len = pattern_len > 1 ? pattern_len - 1 : 0;
    static thread_local std::vector<char> buffer;
    const size_t buffer_size = FAST_BUFFER_SIZE + overlap_len;
    if (buffer.size() < buffer_size) {
        buffer.resize(buffer_size);
    }
    size_t carry_len = 0;

    while (true) {
        ssize_t bytes_read = ::read(fd, buffer.data() + carry_len, FAST_BUFFER_SIZE);

        if (bytes_read <= 0) {
            break;
        }

        const char* data = buffer.data();
        const size_t size = carry_len + static_cast<size_t>(bytes_read);

        if (!user_stats.all_files && std::memchr(data, '\0', size) != nullptr) {
            output.resize(output_start);
            ::close(fd);
            return 0;
        }

        if (size >= pattern_len &&
            ::memmem(data, size, pattern.data(), pattern_len) != nullptr) {
                if (user_stats.cool_colors) {
                    append_colored_path(output, path, get_name_pos(), user_stats.colors);
                    output.push_back('\n');
                } else {
                    append_plain_path(output, path, get_name_pos());
                    output.push_back('\n');
                }

                ++local_matches;
                ::close(fd);
                return local_matches;
        }

        if (pattern_len > 1) {
            carry_len = std::min(overlap_len, size);
            std::memmove(buffer.data(), data + size - carry_len, carry_len);
        }
    }

    ::close(fd);
    return local_matches;
}

size_t read_file_line_options(const std::string& path, UserOptions& user_stats, std::string& output)
{
        constexpr size_t LINE_BUFFER_SIZE = 128 * 1024;

        int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd == -1) {
            return 0;
        }

        const size_t output_start = output.size();
        size_t local_matches = 0;
        size_t cached_name_pos = std::string::npos;
        auto get_name_pos = [&]() {
            if (cached_name_pos == std::string::npos) {
                cached_name_pos = filename_pos(path);
            }
            return cached_name_pos;
        };

        std::array<char, LINE_BUFFER_SIZE> buffer;
        std::string pending_line;
        pending_line.reserve(4096);

        std::deque<std::string> prev_lines;

        const unsigned int before = user_stats.print_before_source;
        const unsigned int after = user_stats.print_after_source;
        const std::string& pattern = user_stats.pattern;
        const size_t pattern_len = pattern.size();
        const bool sparse_line_prefilter = before == 0 && after == 0;
        const bool hit_driven_line_scan = sparse_line_prefilter && user_stats.max_lines == 0;
        unsigned int after_remaining = 0;
        size_t line_num = 0;

        auto emit_match = [&](
            const char* line_data,
            size_t line_len,
            size_t match_line_num,
            const char* hit
        ) {
            if (user_stats.cool_colors) {
                append_colored_path(output, path, get_name_pos(), user_stats.colors);

                if (user_stats.source_print) {
                    output.push_back('\n');
                    if (user_stats.line_number_print) {
                        output.append(user_stats.colors.line);
                        output.append(std::to_string(match_line_num));
                        output.append(":");
                        output.append(RESET);
                        output.push_back('\t');
                    }
                    append_highlighted_source(
                        output,
                        line_data,
                        line_len,
                        hit,
                        pattern_len,
                        user_stats.colors
                    );
                } else if (user_stats.line_number_print) {
                    output.push_back(' ');
                    output.append(user_stats.colors.line);
                    output.append(std::to_string(match_line_num));
                    output.append(":");
                    output.append(RESET);
                }
            } else {
                append_plain_path(output, path, get_name_pos());

                if (user_stats.source_print) {
                    output.push_back('\n');
                    if (user_stats.line_number_print) {
                        output.append(std::to_string(match_line_num));
                        output.append(":");
                        output.push_back('\t');
                    }
                    output.append(line_data, line_len);
                } else if (user_stats.line_number_print) {
                    output.push_back(' ');
                    output.append(std::to_string(match_line_num));
                    output.append(":");
                }
            }
            output.append(user_stats.add_newline ? "\n\n" : "\n");
            ++local_matches;
        };

        auto process_line = [&](const char* line_data, size_t line_len) {
            ++line_num;
            const char* hit = nullptr;

            if (line_len >= pattern_len) {
                hit = static_cast<const char*>(
                    ::memmem(line_data, line_len, pattern.data(), pattern_len)
                );
            }

            if (hit != nullptr) {
                if (before > 0) {
                    for (const auto& pline : prev_lines) {
                        output.append(pline);
                        output.push_back('\n');
                    }
                }

                emit_match(line_data, line_len, line_num, hit);
                after_remaining = after;
            }
            else if (after_remaining > 0) {
                output.append(line_data, line_len);
                output.push_back('\n');
                --after_remaining;
            }
            if (before > 0) {
                if (prev_lines.size() == before) {
                    prev_lines.pop_front();
                }
                prev_lines.emplace_back(line_data, line_len);
            }
            if (user_stats.max_lines > 0 && line_num >= user_stats.max_lines) {
                return;
            }
        };

        bool stop = false;

        while (!stop) {
            ssize_t bytes_read = ::read(fd, buffer.data(), buffer.size());

            if (bytes_read <= 0) {
                break;
            }

            const char* chunk_start = buffer.data();
            const char* chunk_end = buffer.data() + bytes_read;
            const char* line_start = chunk_start;

            if (!user_stats.all_files && std::memchr(chunk_start, '\0', bytes_read) != nullptr) {
                output.resize(output_start);
                ::close(fd);
                return 0;
            }

            if (hit_driven_line_scan && pending_line.empty()) {
                const char* scan_pos = chunk_start;
                const char* count_pos = chunk_start;
                const char* current_line_start = chunk_start;
                const char* last_emitted_line_start = nullptr;
                bool carried_tail = false;

                while (scan_pos < chunk_end) {
                    const void* hit_ptr = ::memmem(
                        scan_pos,
                        chunk_end - scan_pos,
                        pattern.data(),
                        pattern_len
                    );

                    if (hit_ptr == nullptr) {
                        break;
                    }

                    const char* hit = static_cast<const char*>(hit_ptr);

                    while (count_pos < hit) {
                        const void* newline_hit = std::memchr(count_pos, '\n', hit - count_pos);
                        if (newline_hit == nullptr) {
                            break;
                        }

                        const char* newline = static_cast<const char*>(newline_hit);
                        ++line_num;
                        current_line_start = newline + 1;
                        count_pos = current_line_start;
                    }

                    const void* line_end_ptr = std::memchr(hit, '\n', chunk_end - hit);
                    if (line_end_ptr == nullptr) {
                        pending_line.append(current_line_start, chunk_end - current_line_start);
                        carried_tail = true;
                        break;
                    }

                    const char* line_end = static_cast<const char*>(line_end_ptr);
                    if (current_line_start != last_emitted_line_start) {
                        emit_match(current_line_start, line_end - current_line_start, line_num + 1, hit);
                        last_emitted_line_start = current_line_start;
                    }

                    ++line_num;
                    current_line_start = line_end + 1;
                    count_pos = current_line_start;
                    scan_pos = current_line_start;
                }

                if (!carried_tail) {
                    while (count_pos < chunk_end) {
                        const void* newline_hit = std::memchr(count_pos, '\n', chunk_end - count_pos);
                        if (newline_hit == nullptr) {
                            pending_line.append(count_pos, chunk_end - count_pos);
                            break;
                        }

                        const char* newline = static_cast<const char*>(newline_hit);
                        ++line_num;
                        count_pos = newline + 1;
                    }
                }

                continue;
            }

            if (sparse_line_prefilter && pending_line.empty() &&
                (static_cast<size_t>(bytes_read) < pattern_len ||
                 ::memmem(chunk_start, static_cast<size_t>(bytes_read), pattern.data(), pattern_len) == nullptr)) {
                while (line_start < chunk_end) {
                    const void* newline_hit = std::memchr(line_start, '\n', chunk_end - line_start);

                    if (!newline_hit) {
                        pending_line.append(line_start, chunk_end - line_start);
                        break;
                    }

                    ++line_num;
                    if (user_stats.max_lines > 0 && line_num >= user_stats.max_lines) {
                        stop = true;
                        break;
                    }

                    line_start = static_cast<const char*>(newline_hit) + 1;
                }
                continue;
            }

            while (line_start < chunk_end) {
                const void* newline_hit = std::memchr(line_start, '\n', chunk_end - line_start);

                if (!newline_hit) {
                    pending_line.append(line_start, chunk_end - line_start);
                    break;
                }

                const char* line_end = static_cast<const char*>(newline_hit);

                if (!pending_line.empty()) {
                    pending_line.append(line_start, line_end - line_start);
                    process_line(pending_line.data(), pending_line.size());
                    pending_line.clear();
                } else {
                    process_line(line_start, line_end - line_start);
                }

                if (user_stats.max_lines > 0 && line_num >= user_stats.max_lines) {
                    stop = true;
                    break;
                }

                line_start = line_end + 1;
            }
        }

        if (!stop && !pending_line.empty()) {
            process_line(pending_line.data(), pending_line.size());
        }

        ::close(fd);

    return local_matches;
}

size_t read_file_count(const std::string& path, UserOptions& user_stats, std::string& output)
{
    constexpr size_t LINE_BUFFER_SIZE = 128 * 1024;

    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
        return 0;
    }

    const size_t output_start = output.size();
    size_t local_matches = 0;
    size_t cached_name_pos = std::string::npos;
    auto get_name_pos = [&]() {
        if (cached_name_pos == std::string::npos) {
            cached_name_pos = filename_pos(path);
        }
        return cached_name_pos;
    };

    std::array<char, LINE_BUFFER_SIZE> buffer;
    std::string pending_line;
    pending_line.reserve(4096);

    const std::string& pattern = user_stats.pattern;
    const size_t pattern_len = pattern.size();
    size_t line_num = 0;
    bool stop = false;

    auto process_line = [&](const char* line_data, size_t line_len) {
        ++line_num;
        if (line_len >= pattern_len &&
            ::memmem(line_data, line_len, pattern.data(), pattern_len) != nullptr) {
            ++local_matches;
        }
        if (user_stats.max_lines > 0 && line_num >= user_stats.max_lines) {
            stop = true;
        }
    };

    while (!stop) {
        ssize_t bytes_read = ::read(fd, buffer.data(), buffer.size());

        if (bytes_read <= 0) {
            break;
        }

        const char* chunk_start = buffer.data();
        const char* chunk_end = buffer.data() + bytes_read;
        const char* line_start = chunk_start;

        if (!user_stats.all_files && std::memchr(chunk_start, '\0', bytes_read) != nullptr) {
            output.resize(output_start);
            ::close(fd);
            return 0;
        }

        while (line_start < chunk_end) {
            const void* newline_hit = std::memchr(line_start, '\n', chunk_end - line_start);

            if (!newline_hit) {
                pending_line.append(line_start, chunk_end - line_start);
                break;
            }

            const char* line_end = static_cast<const char*>(newline_hit);

            if (!pending_line.empty()) {
                pending_line.append(line_start, line_end - line_start);
                process_line(pending_line.data(), pending_line.size());
                pending_line.clear();
            } else {
                process_line(line_start, line_end - line_start);
            }

            if (stop) {
                break;
            }

            line_start = line_end + 1;
        }
    }

    if (!stop && !pending_line.empty()) {
        process_line(pending_line.data(), pending_line.size());
    }

    ::close(fd);

    if (local_matches > 0) {
        if (user_stats.cool_colors) {
            append_colored_path(output, path, get_name_pos(), user_stats.colors);
        } else {
            append_plain_path(output, path, get_name_pos());
        }
        output.push_back('\t');
        output.append(std::to_string(local_matches));
        output.push_back('\n');
    }

    return local_matches;
}

ReadFileFn choose_read_file(const UserOptions& user_stats)
{
    if (user_stats.count_print) {
        return read_file_count;
    }

    if (!user_stats.source_print &&
        !user_stats.line_number_print &&
        user_stats.print_before_source == 0 &&
        user_stats.print_after_source == 0 &&
        user_stats.max_lines == 0) {
            return read_file_fast;
    }

    return read_file_line_options;
}

size_t search_files_single_thread(
    const std::vector<std::string>& paths,
    UserOptions& user_stats,
    ReadFileFn read_file
)
{
    std::string output;
    output.reserve(8192);

    size_t local_matches = 0;

    for (const auto& path : paths) {
        local_matches += read_file(path, user_stats, output);

        if (output.size() >= OUTPUT_FLUSH_SIZE) {
            cout << output;
            matches += local_matches;
            output.clear();
            local_matches = 0;
        }
    }

    cout << output;
    matches += local_matches;

    return paths.empty() ? 0 : 1;
}

size_t search_stdin(UserOptions& user_stats)
{
    constexpr size_t LINE_BUFFER_SIZE = 128 * 1024;

    std::array<char, LINE_BUFFER_SIZE> buffer;
    std::string output;
    output.reserve(8192);

    std::string pending_line;
    pending_line.reserve(4096);

    std::deque<std::string> prev_lines;

    const unsigned int before = user_stats.print_before_source;
    const unsigned int after = user_stats.print_after_source;
    const std::string& pattern = user_stats.pattern;
    const size_t pattern_len = pattern.size();
    const bool sparse_line_prefilter = before == 0 && after == 0;
    const bool hit_driven_line_scan = sparse_line_prefilter && user_stats.max_lines == 0;

    size_t local_matches = 0;
    size_t line_num = 0;
    unsigned int after_remaining = 0;
    bool stop = false;

    if (user_stats.count_print) {
        auto process_count_line = [&](const char* line_data, size_t line_len) {
            ++line_num;
            if (line_len >= pattern_len &&
                ::memmem(line_data, line_len, pattern.data(), pattern_len) != nullptr) {
                ++local_matches;
            }
            if (user_stats.max_lines > 0 && line_num >= user_stats.max_lines) {
                stop = true;
            }
        };

        while (!stop) {
            ssize_t bytes_read = ::read(STDIN_FILENO, buffer.data(), buffer.size());

            if (bytes_read <= 0) {
                break;
            }

            const char* chunk_start = buffer.data();
            const char* chunk_end = buffer.data() + bytes_read;
            const char* line_start = chunk_start;

            while (line_start < chunk_end) {
                const void* newline_hit = std::memchr(line_start, '\n', chunk_end - line_start);

                if (!newline_hit) {
                    pending_line.append(line_start, chunk_end - line_start);
                    break;
                }

                const char* line_end = static_cast<const char*>(newline_hit);

                if (!pending_line.empty()) {
                    pending_line.append(line_start, line_end - line_start);
                    process_count_line(pending_line.data(), pending_line.size());
                    pending_line.clear();
                } else {
                    process_count_line(line_start, line_end - line_start);
                }

                if (stop) {
                    break;
                }

                line_start = line_end + 1;
            }
        }

        if (!stop && !pending_line.empty()) {
            process_count_line(pending_line.data(), pending_line.size());
        }

        cout << local_matches << '\n';
        matches += local_matches;

        return local_matches;
    }

    auto flush_if_needed = [&]() {
        if (output.size() >= OUTPUT_FLUSH_SIZE) {
            cout << output;
            output.clear();
        }
    };

    auto emit_match = [&](
        const char* line_data,
        size_t line_len,
        size_t match_line_num,
        const char* hit
    ) {
        if (user_stats.line_number_print) {
            if (user_stats.cool_colors) {
                output.append(user_stats.colors.line);
                output.append(std::to_string(match_line_num));
                output.append(":");
                output.append(RESET);
            } else {
                output.append(std::to_string(match_line_num));
                output.append(":");
            }
            output.push_back('\t');
        }

        if (user_stats.cool_colors) {
            append_highlighted_source(
                output,
                line_data,
                line_len,
                hit,
                pattern_len,
                user_stats.colors
            );
        } else {
            output.append(line_data, line_len);
        }

        output.append(user_stats.add_newline ? "\n\n" : "\n");
        ++local_matches;
        flush_if_needed();
    };

    auto process_line = [&](const char* line_data, size_t line_len) {
        ++line_num;
        const char* hit = nullptr;

        if (line_len >= pattern_len) {
            hit = static_cast<const char*>(
                ::memmem(line_data, line_len, pattern.data(), pattern_len)
            );
        }

        if (hit != nullptr) {
            if (before > 0) {
                for (const auto& pline : prev_lines) {
                    output.append(pline);
                    output.push_back('\n');
                    flush_if_needed();
                }
            }

            emit_match(line_data, line_len, line_num, hit);
            after_remaining = after;
        }
        else if (after_remaining > 0) {
            output.append(line_data, line_len);
            output.push_back('\n');
            --after_remaining;
            flush_if_needed();
        }

        if (before > 0) {
            if (prev_lines.size() == before) {
                prev_lines.pop_front();
            }
            prev_lines.emplace_back(line_data, line_len);
        }

        if (user_stats.max_lines > 0 && line_num >= user_stats.max_lines) {
            stop = true;
        }
    };

    while (!stop) {
        ssize_t bytes_read = ::read(STDIN_FILENO, buffer.data(), buffer.size());

        if (bytes_read <= 0) {
            break;
        }

        const char* chunk_start = buffer.data();
        const char* chunk_end = buffer.data() + bytes_read;
        const char* line_start = chunk_start;

        if (hit_driven_line_scan && pending_line.empty()) {
            const char* scan_pos = chunk_start;
            const char* count_pos = chunk_start;
            const char* current_line_start = chunk_start;
            const char* last_emitted_line_start = nullptr;
            bool carried_tail = false;

            while (scan_pos < chunk_end) {
                const void* hit_ptr = ::memmem(
                    scan_pos,
                    chunk_end - scan_pos,
                    pattern.data(),
                    pattern_len
                );

                if (hit_ptr == nullptr) {
                    break;
                }

                const char* hit = static_cast<const char*>(hit_ptr);

                while (count_pos < hit) {
                    const void* newline_hit = std::memchr(count_pos, '\n', hit - count_pos);
                    if (newline_hit == nullptr) {
                        break;
                    }

                    const char* newline = static_cast<const char*>(newline_hit);
                    ++line_num;
                    current_line_start = newline + 1;
                    count_pos = current_line_start;
                }

                const void* line_end_ptr = std::memchr(hit, '\n', chunk_end - hit);
                if (line_end_ptr == nullptr) {
                    pending_line.append(current_line_start, chunk_end - current_line_start);
                    carried_tail = true;
                    break;
                }

                const char* line_end = static_cast<const char*>(line_end_ptr);
                if (current_line_start != last_emitted_line_start) {
                    emit_match(current_line_start, line_end - current_line_start, line_num + 1, hit);
                    last_emitted_line_start = current_line_start;
                }

                ++line_num;
                current_line_start = line_end + 1;
                count_pos = current_line_start;
                scan_pos = current_line_start;
            }

            if (!carried_tail) {
                while (count_pos < chunk_end) {
                    const void* newline_hit = std::memchr(count_pos, '\n', chunk_end - count_pos);
                    if (newline_hit == nullptr) {
                        pending_line.append(count_pos, chunk_end - count_pos);
                        break;
                    }

                    const char* newline = static_cast<const char*>(newline_hit);
                    ++line_num;
                    count_pos = newline + 1;
                }
            }

            continue;
        }

        if (sparse_line_prefilter && pending_line.empty() &&
            (static_cast<size_t>(bytes_read) < pattern_len ||
             ::memmem(chunk_start, static_cast<size_t>(bytes_read), pattern.data(), pattern_len) == nullptr)) {
            while (line_start < chunk_end) {
                const void* newline_hit = std::memchr(line_start, '\n', chunk_end - line_start);

                if (!newline_hit) {
                    pending_line.append(line_start, chunk_end - line_start);
                    break;
                }

                ++line_num;
                if (user_stats.max_lines > 0 && line_num >= user_stats.max_lines) {
                    stop = true;
                    break;
                }

                line_start = static_cast<const char*>(newline_hit) + 1;
            }
            continue;
        }

        while (line_start < chunk_end) {
            const void* newline_hit = std::memchr(line_start, '\n', chunk_end - line_start);

            if (!newline_hit) {
                pending_line.append(line_start, chunk_end - line_start);
                break;
            }

            const char* line_end = static_cast<const char*>(newline_hit);

            if (!pending_line.empty()) {
                pending_line.append(line_start, line_end - line_start);
                process_line(pending_line.data(), pending_line.size());
                pending_line.clear();
            } else {
                process_line(line_start, line_end - line_start);
            }

            if (user_stats.max_lines > 0 && line_num >= user_stats.max_lines) {
                stop = true;
                break;
            }

            line_start = line_end + 1;
        }
    }

    if (!stop && !pending_line.empty()) {
        process_line(pending_line.data(), pending_line.size());
    }

    cout << output;
    matches += local_matches;

    return local_matches;
}

int exit_code_from_matches()
{
    return matches > 0 ? MGREP_EXIT_MATCH_FOUND : MGREP_EXIT_NO_MATCH;
}
