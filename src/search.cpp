#include "search.hpp"

#include "output.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <sys/uio.h>
#include <unistd.h>

#if defined(__GNUC__) && (defined(__SSE2__) || defined(__x86_64__) || defined(__i386__))
#include <immintrin.h>
#endif

using std::cout;

size_t matches = 0;

namespace {

thread_local DirectOutputFn direct_output_fn = nullptr;
thread_local void* direct_output_context = nullptr;

#if defined(__GNUC__)
#define MGREP_COLD_NOINLINE __attribute__((cold, noinline))
#else
#define MGREP_COLD_NOINLINE
#endif

enum class FastScanResult {
    NoMatch,
    Match,
    Binary
};

inline char fold_ascii(char ch)
{
    return ch >= 'A' && ch <= 'Z'
        ? static_cast<char>(ch + ('a' - 'A'))
        : ch;
}

bool folded_match_at(const char* data, const char* folded_pattern, size_t pattern_len)
{
    for (size_t i = 1; i < pattern_len; ++i) {
        if (fold_ascii(data[i]) != folded_pattern[i]) {
            return false;
        }
    }
    return true;
}

const char* find_case_insensitive(
    const char* data,
    size_t size,
    const char* folded_pattern,
    size_t pattern_len
)
{
    if (pattern_len == 0) {
        return data;
    }
    if (size < pattern_len) {
        return nullptr;
    }

    const char first = folded_pattern[0];
    const size_t last_start = size - pattern_len;
    for (size_t pos = 0; pos <= last_start; ++pos) {
        if (fold_ascii(data[pos]) == first &&
            (pattern_len == 1 || folded_match_at(data + pos, folded_pattern, pattern_len))) {
            return data + pos;
        }
    }

    return nullptr;
}

const char* find_match(const char* data, size_t size, const UserOptions& user_stats)
{
    const size_t pattern_len = user_stats.pattern.size();
    if (pattern_len == 0) {
        return data;
    }
    if (size < pattern_len) {
        return nullptr;
    }
    if (user_stats.ignore_case) {
        return find_case_insensitive(data, size, user_stats.folded_pattern.data(), pattern_len);
    }
    return static_cast<const char*>(
        ::memmem(data, size, user_stats.pattern.data(), pattern_len)
    );
}

bool contains_match(const char* data, size_t size, const UserOptions& user_stats)
{
    return find_match(data, size, user_stats) != nullptr;
}

FastScanResult scan_text_chunk_case_insensitive(
    const char* data,
    size_t size,
    const char* folded_pattern,
    size_t pattern_len
)
{
    if (std::memchr(data, '\0', size) != nullptr) {
        return FastScanResult::Binary;
    }
    return find_case_insensitive(data, size, folded_pattern, pattern_len) != nullptr
        ? FastScanResult::Match
        : FastScanResult::NoMatch;
}

#if defined(__GNUC__) && (defined(__SSE2__) || defined(__x86_64__) || defined(__i386__))
__attribute__((target("avx2")))
FastScanResult scan_text_chunk_avx2(
    const char* data,
    size_t size,
    const char* pattern,
    size_t pattern_len
)
{
    const __m256i zero = _mm256_setzero_si256();
    const __m256i first = _mm256_set1_epi8(pattern_len == 0 ? '\0' : pattern[0]);
    size_t pos = 0;
    bool found = false;

    while (pos + 32 <= size) {
        const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + pos));
        const int zero_mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(bytes, zero));
        if (zero_mask != 0) {
            return FastScanResult::Binary;
        }

        uint32_t first_mask = static_cast<uint32_t>(
            _mm256_movemask_epi8(_mm256_cmpeq_epi8(bytes, first))
        );
        while (first_mask != 0) {
            const uint32_t bit = first_mask & (0U - first_mask);
            const size_t candidate = pos + static_cast<size_t>(__builtin_ctz(first_mask));
            if (candidate + pattern_len <= size &&
                (pattern_len == 1 || std::memcmp(data + candidate + 1, pattern + 1, pattern_len - 1) == 0)) {
                found = true;
            }
            first_mask &= ~bit;
        }

        pos += 32;
    }

    for (; pos < size; ++pos) {
        if (data[pos] == '\0') {
            return FastScanResult::Binary;
        }
        if (data[pos] == pattern[0] &&
            pos + pattern_len <= size &&
            (pattern_len == 1 || std::memcmp(data + pos + 1, pattern + 1, pattern_len - 1) == 0)) {
            found = true;
        }
    }

    return found ? FastScanResult::Match : FastScanResult::NoMatch;
}

FastScanResult scan_text_chunk_sse2(
    const char* data,
    size_t size,
    const char* pattern,
    size_t pattern_len
)
{
    const __m128i zero = _mm_setzero_si128();
    const __m128i first = _mm_set1_epi8(pattern_len == 0 ? '\0' : pattern[0]);
    size_t pos = 0;
    bool found = false;

    while (pos + 16 <= size) {
        const __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + pos));
        const int zero_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(bytes, zero));
        if (zero_mask != 0) {
            return FastScanResult::Binary;
        }

        int first_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(bytes, first));
        while (first_mask != 0) {
            const unsigned int bit = static_cast<unsigned int>(first_mask) &
                (0U - static_cast<unsigned int>(first_mask));
            const size_t candidate = pos + static_cast<size_t>(__builtin_ctz(static_cast<unsigned int>(first_mask)));
            if (candidate + pattern_len <= size &&
                (pattern_len == 1 || std::memcmp(data + candidate + 1, pattern + 1, pattern_len - 1) == 0)) {
                found = true;
            }
            first_mask &= ~static_cast<int>(bit);
        }

        pos += 16;
    }

    for (; pos < size; ++pos) {
        if (data[pos] == '\0') {
            return FastScanResult::Binary;
        }
        if (data[pos] == pattern[0] &&
            pos + pattern_len <= size &&
            (pattern_len == 1 || std::memcmp(data + pos + 1, pattern + 1, pattern_len - 1) == 0)) {
            found = true;
        }
    }

    return found ? FastScanResult::Match : FastScanResult::NoMatch;
}
#endif

FastScanResult scan_text_chunk(
    const char* data,
    size_t size,
    const char* pattern,
    size_t pattern_len
)
{
    if (pattern_len == 0) {
        return std::memchr(data, '\0', size) == nullptr
            ? FastScanResult::Match
            : FastScanResult::Binary;
    }

#if defined(__GNUC__) && (defined(__SSE2__) || defined(__x86_64__) || defined(__i386__))
#if defined(__x86_64__) || defined(__i386__)
    if (__builtin_cpu_supports("avx2")) {
        return scan_text_chunk_avx2(data, size, pattern, pattern_len);
    }
#endif
    return scan_text_chunk_sse2(data, size, pattern, pattern_len);
#else
    if (std::memchr(data, '\0', size) != nullptr) {
        return FastScanResult::Binary;
    }
    return std::memmem(data, size, pattern, pattern_len) != nullptr
        ? FastScanResult::Match
        : FastScanResult::NoMatch;
#endif
}

void write_direct_stdout(
    std::string& output,
    const DirectOutputPiece* pieces,
    size_t piece_count
)
{
    std::array<iovec, 16> iov{};
    size_t iov_count = 0;

    if (!output.empty()) {
        iov[iov_count++] = {
            const_cast<char*>(output.data()),
            output.size()
        };
    }

    for (size_t i = 0; i < piece_count && iov_count < iov.size(); ++i) {
        if (pieces[i].size == 0) {
            continue;
        }

        iov[iov_count++] = {
            const_cast<char*>(pieces[i].data),
            pieces[i].size
        };
    }

    size_t iov_start = 0;
    while (iov_start < iov_count) {
        const ssize_t bytes_written = ::writev(
            STDOUT_FILENO,
            iov.data() + iov_start,
            static_cast<int>(iov_count - iov_start)
        );
        if (bytes_written < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (bytes_written == 0) {
            break;
        }

        size_t remaining = static_cast<size_t>(bytes_written);
        while (iov_start < iov_count && remaining >= iov[iov_start].iov_len) {
            remaining -= iov[iov_start].iov_len;
            ++iov_start;
        }
        if (remaining > 0 && iov_start < iov_count) {
            iov[iov_start].iov_base = static_cast<char*>(iov[iov_start].iov_base) + remaining;
            iov[iov_start].iov_len -= remaining;
        }
    }

    output.clear();
}

void direct_output_single(
    void*,
    std::string& output,
    const DirectOutputPiece* pieces,
    size_t piece_count
)
{
    cout.flush();
    write_direct_stdout(output, pieces, piece_count);
}

bool direct_output_available()
{
    return direct_output_fn != nullptr;
}

MGREP_COLD_NOINLINE void emit_direct_source(
    std::string& output,
    const char* line_data,
    size_t line_len,
    const char* hit,
    size_t pattern_len,
    const ColorTheme& colors,
    bool cool_colors,
    const char* suffix,
    size_t suffix_len
)
{
    std::array<DirectOutputPiece, 10> pieces{};
    size_t piece_count = 0;

    if (!cool_colors || hit == nullptr || hit < line_data || hit + pattern_len > line_data + line_len) {
        if (cool_colors && !colors.source.empty()) {
            pieces[piece_count++] = {colors.source.data(), colors.source.size()};
        }
        pieces[piece_count++] = {line_data, line_len};
        if (cool_colors && !colors.source.empty()) {
            pieces[piece_count++] = {RESET, std::strlen(RESET)};
        }
        pieces[piece_count++] = {suffix, suffix_len};
        direct_output_fn(direct_output_context, output, pieces.data(), piece_count);
        return;
    }

    if (!colors.source.empty()) {
        pieces[piece_count++] = {colors.source.data(), colors.source.size()};
    }
    pieces[piece_count++] = {line_data, static_cast<size_t>(hit - line_data)};
    pieces[piece_count++] = {colors.match.data(), colors.match.size()};
    pieces[piece_count++] = {hit, pattern_len};
    pieces[piece_count++] = {RESET, std::strlen(RESET)};
    if (!colors.source.empty()) {
        pieces[piece_count++] = {colors.source.data(), colors.source.size()};
    }
    pieces[piece_count++] = {
        hit + pattern_len,
        static_cast<size_t>(line_data + line_len - hit - pattern_len)
    };
    if (!colors.source.empty()) {
        pieces[piece_count++] = {RESET, std::strlen(RESET)};
    }
    pieces[piece_count++] = {suffix, suffix_len};
    direct_output_fn(direct_output_context, output, pieces.data(), piece_count);
}

MGREP_COLD_NOINLINE bool try_emit_direct_match_source(
    std::string& output,
    const std::string& path,
    size_t name_pos,
    const char* line_data,
    size_t line_len,
    size_t match_line_num,
    const char* hit,
    size_t pattern_len,
    UserOptions& user_stats,
    bool emits_source
)
{
    if (!emits_source || line_len < OUTPUT_FLUSH_SIZE || !direct_output_available()) {
        return false;
    }

    if (user_stats.cool_colors) {
        append_colored_path(output, path, name_pos, user_stats.colors);
        output.push_back('\n');
        if (user_stats.line_number_print) {
            output.append(user_stats.colors.line);
            output.append(std::to_string(match_line_num));
            output.append(":");
            output.append(RESET);
            output.push_back('\t');
        }
    } else {
        append_plain_path(output, path, name_pos);
        output.push_back('\n');
        if (user_stats.line_number_print) {
            output.append(std::to_string(match_line_num));
            output.append(":");
            output.push_back('\t');
        }
    }

    const char* suffix = user_stats.add_newline ? "\n\n" : "\n";
    emit_direct_source(
        output,
        line_data,
        line_len,
        hit,
        pattern_len,
        user_stats.colors,
        user_stats.cool_colors,
        suffix,
        user_stats.add_newline ? 2 : 1
    );
    return true;
}

#undef MGREP_COLD_NOINLINE

} // namespace

void set_direct_output_context(DirectOutputFn fn, void* context)
{
    direct_output_fn = fn;
    direct_output_context = context;
}

void clear_direct_output_context()
{
    direct_output_fn = nullptr;
    direct_output_context = nullptr;
}

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
    bool found_match = false;

    while (true) {
        ssize_t bytes_read = ::read(fd, buffer.data() + carry_len, FAST_BUFFER_SIZE);

        if (bytes_read <= 0) {
            break;
        }

        const char* data = buffer.data();
        const size_t size = carry_len + static_cast<size_t>(bytes_read);

        if (found_match) {
            if (std::memchr(data + carry_len, '\0', static_cast<size_t>(bytes_read)) != nullptr) {
                output.resize(output_start);
                ::close(fd);
                return 0;
            }
            carry_len = 0;
            continue;
        }

        bool found = false;
        if (user_stats.all_files) {
            found = contains_match(data, size, user_stats);
        } else {
            const FastScanResult scan_result =
                user_stats.ignore_case
                    ? scan_text_chunk_case_insensitive(
                        data,
                        size,
                        user_stats.folded_pattern.data(),
                        pattern_len
                    )
                    : scan_text_chunk(data, size, pattern.data(), pattern_len);
            if (scan_result == FastScanResult::Binary) {
                output.resize(output_start);
                ::close(fd);
                return 0;
            }
            found = scan_result == FastScanResult::Match;
        }

        if (found) {
            if (!user_stats.all_files) {
                found_match = true;
                carry_len = 0;
                continue;
            }

            if (user_stats.quiet) {
                ++local_matches;
                ::close(fd);
                return local_matches;
            }

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

    if (found_match) {
        if (user_stats.quiet) {
            return 1;
        }

        if (user_stats.cool_colors) {
            append_colored_path(output, path, get_name_pos(), user_stats.colors);
            output.push_back('\n');
        } else {
            append_plain_path(output, path, get_name_pos());
            output.push_back('\n');
        }

        return 1;
    }

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
        size_t pending_hit_offset = std::string::npos;

        std::deque<std::string> prev_lines;

        const unsigned int before = user_stats.print_before_source;
        const unsigned int after = user_stats.print_after_source;
        const std::string& pattern = user_stats.pattern;
        const size_t pattern_len = pattern.size();
        const bool sparse_line_prefilter = before == 0 && after == 0 && !user_stats.invert_match;
        const bool hit_driven_line_scan =
            sparse_line_prefilter &&
            user_stats.max_lines == 0 &&
            !user_stats.invert_match &&
            !user_stats.heading;
        unsigned int after_remaining = 0;
        size_t line_num = 0;
        size_t last_output_line_num = 0;
        const bool invert_path_only =
            user_stats.invert_match &&
            !user_stats.source_print &&
            !user_stats.line_number_print &&
            before == 0 &&
            after == 0;
        const bool context_print = before > 0 || after > 0;
        const bool heading_mode =
            user_stats.heading &&
            !user_stats.count_print &&
            !user_stats.only_matching &&
            !user_stats.quiet;
        bool stop = false;
        bool heading_printed = false;

        auto ensure_heading = [&]() {
            if (!heading_mode || heading_printed) {
                return;
            }
            if (output.size() > output_start && output.back() != '\n') {
                output.push_back('\n');
            }
            if (user_stats.cool_colors) {
                append_colored_path(output, path, get_name_pos(), user_stats.colors);
            } else {
                append_plain_path(output, path, get_name_pos());
            }
            output.push_back('\n');
            heading_printed = true;
        };

        auto append_line_prefix = [&](size_t match_line_num) {
            if (!user_stats.line_number_print) {
                return;
            }
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
        };

        auto append_pending_line = [&](const char* data, size_t len) {
            if (len == 0) {
                return;
            }

            const size_t old_size = pending_line.size();
            const size_t new_size = old_size + len;
            if (new_size > pending_line.capacity() && old_size >= LINE_BUFFER_SIZE) {
                pending_line.reserve(new_size + LINE_BUFFER_SIZE);
            }
            pending_line.append(data, len);

            if (pending_hit_offset != std::string::npos || pattern_len == 0) {
                return;
            }

            const size_t scan_start =
                old_size > pattern_len - 1 ? old_size - (pattern_len - 1) : 0;
            const char* hit = find_match(
                pending_line.data() + scan_start,
                pending_line.size() - scan_start,
                user_stats
            );
            if (hit != nullptr) {
                pending_hit_offset = static_cast<size_t>(hit - pending_line.data());
            }
        };

        auto emit_match = [&](
            const char* line_data,
            size_t line_len,
            size_t match_line_num,
            const char* hit
        ) {
            if (user_stats.quiet) {
                ++local_matches;
                return;
            }

            if (line_len > LINE_BUFFER_SIZE && output.capacity() - output.size() < line_len + 128) {
                output.reserve(output.size() + line_len + path.size() + 256);
            }

            if (invert_path_only) {
                if (local_matches == 0) {
                    if (user_stats.cool_colors) {
                        append_colored_path(output, path, get_name_pos(), user_stats.colors);
                    } else {
                        append_plain_path(output, path, get_name_pos());
                    }
                    output.push_back('\n');
                }
                ++local_matches;
                return;
            }

            const bool emits_source = user_stats.source_print || context_print;
            if (!heading_mode &&
                user_stats.all_files &&
                __builtin_expect(line_len >= OUTPUT_FLUSH_SIZE, 0) &&
                try_emit_direct_match_source(
                    output,
                    path,
                    get_name_pos(),
                    line_data,
                    line_len,
                    match_line_num,
                    hit,
                    pattern_len,
                    user_stats,
                    emits_source
                )) {
                ++local_matches;
                last_output_line_num = match_line_num;
                return;
            }

            if (heading_mode) {
                ensure_heading();
                if (emits_source) {
                    append_line_prefix(match_line_num);
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
                } else if (user_stats.line_number_print) {
                    if (user_stats.cool_colors) {
                        output.append(user_stats.colors.line);
                        output.append(std::to_string(match_line_num));
                        output.append(":");
                        output.append(RESET);
                    } else {
                        output.append(std::to_string(match_line_num));
                        output.append(":");
                    }
                }
            } else if (user_stats.cool_colors) {
                append_colored_path(output, path, get_name_pos(), user_stats.colors);

                if (emits_source) {
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

                if (emits_source) {
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
            last_output_line_num = match_line_num;
        };

        auto process_line_with_hit = [&](const char* line_data, size_t line_len, const char* hit) {
            ++line_num;
            const bool selected = user_stats.invert_match ? hit == nullptr : hit != nullptr;

            if (selected) {
                if (before > 0) {
                    ensure_heading();
                    size_t context_line_num = line_num - prev_lines.size();
                    for (const auto& pline : prev_lines) {
                        if (context_line_num > last_output_line_num) {
                            output.append(pline);
                            output.push_back('\n');
                            last_output_line_num = context_line_num;
                        }
                        ++context_line_num;
                    }
                }

                emit_match(line_data, line_len, line_num, hit);
                after_remaining = after;
                if (user_stats.quiet && local_matches > 0) {
                    stop = true;
                }
            }
            else if (after_remaining > 0) {
                if (line_num > last_output_line_num) {
                    ensure_heading();
                    output.append(line_data, line_len);
                    output.push_back('\n');
                    last_output_line_num = line_num;
                }
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

        auto process_line = [&](const char* line_data, size_t line_len) {
            const char* hit = nullptr;

            if (pattern_len == 0) {
                hit = line_data;
            } else if (line_len >= pattern_len) {
                hit = find_match(line_data, line_len, user_stats);
            }

            process_line_with_hit(line_data, line_len, hit);
        };

        auto process_pending_line = [&]() {
            const char* hit = nullptr;
            if (pattern_len == 0) {
                hit = pending_line.data();
            } else if (pending_hit_offset != std::string::npos &&
                       pending_hit_offset + pattern_len <= pending_line.size()) {
                hit = pending_line.data() + pending_hit_offset;
            }

            process_line_with_hit(pending_line.data(), pending_line.size(), hit);
            pending_line.clear();
            pending_hit_offset = std::string::npos;
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

            if (hit_driven_line_scan && pending_line.empty()) {
                const char* scan_pos = chunk_start;
                const char* count_pos = chunk_start;
                const char* current_line_start = chunk_start;
                const char* last_emitted_line_start = nullptr;
                bool carried_tail = false;

                while (scan_pos < chunk_end) {
                    const char* hit = find_match(
                        scan_pos,
                        chunk_end - scan_pos,
                        user_stats
                    );

                    if (hit == nullptr) {
                        break;
                    }

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
                        pending_hit_offset = static_cast<size_t>(hit - current_line_start);
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
                            pending_hit_offset = std::string::npos;
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
                 !contains_match(chunk_start, static_cast<size_t>(bytes_read), user_stats))) {
                while (line_start < chunk_end) {
                    const void* newline_hit = std::memchr(line_start, '\n', chunk_end - line_start);

                    if (!newline_hit) {
                        append_pending_line(line_start, chunk_end - line_start);
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
                    append_pending_line(line_start, chunk_end - line_start);
                    break;
                }

                const char* line_end = static_cast<const char*>(newline_hit);

                if (!pending_line.empty()) {
                    append_pending_line(line_start, line_end - line_start);
                    process_pending_line();
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
            process_pending_line();
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
        const bool contains = contains_match(line_data, line_len, user_stats);
        if (user_stats.invert_match ? !contains : contains) {
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

    if (local_matches > 0 && !user_stats.quiet) {
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

size_t read_file_only_matching(const std::string& path, UserOptions& user_stats, std::string& output)
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

    auto emit_occurrence = [&](size_t match_line_num, const char* hit) {
        if (user_stats.cool_colors) {
            append_colored_path(output, path, get_name_pos(), user_stats.colors);
        } else {
            append_plain_path(output, path, get_name_pos());
        }

        if (user_stats.line_number_print) {
            output.push_back(' ');
            if (user_stats.cool_colors) {
                output.append(user_stats.colors.line);
                output.append(std::to_string(match_line_num));
                output.append(":");
                output.append(RESET);
            } else {
                output.append(std::to_string(match_line_num));
                output.append(":");
            }
        }

        output.push_back('\t');
        if (user_stats.cool_colors) {
            output.append(user_stats.colors.match);
            output.append(hit, pattern_len);
            output.append(RESET);
        } else {
            output.append(hit, pattern_len);
        }
        output.append(user_stats.add_newline ? "\n\n" : "\n");
        ++local_matches;
    };

    auto process_line = [&](const char* line_data, size_t line_len) {
        ++line_num;
        if (pattern_len == 0) {
            ++local_matches;
            if (user_stats.max_lines > 0 && line_num >= user_stats.max_lines) {
                stop = true;
            }
            return;
        }

        const char* scan_pos = line_data;
        size_t remaining = line_len;

        while (remaining >= pattern_len) {
            const char* hit = find_match(scan_pos, remaining, user_stats);
            if (hit == nullptr) {
                break;
            }

            emit_occurrence(line_num, hit);
            const size_t advance = static_cast<size_t>(hit - scan_pos) + pattern_len;
            scan_pos += advance;
            remaining -= advance;
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
    return local_matches;
}

ReadFileFn choose_read_file(const UserOptions& user_stats)
{
    if (user_stats.quiet && (user_stats.invert_match || user_stats.max_lines > 0)) {
        return read_file_line_options;
    }

    if (user_stats.quiet) {
        return read_file_fast;
    }

    if (user_stats.count_print) {
        return read_file_count;
    }

    if (user_stats.invert_match) {
        return read_file_line_options;
    }

    if (user_stats.only_matching) {
        return read_file_only_matching;
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
    set_direct_output_context(direct_output_single, nullptr);

    for (const auto& path : paths) {
        if (user_stats.quiet && local_matches > 0) {
            break;
        }

        local_matches += read_file(path, user_stats, output);

        if (user_stats.quiet && local_matches > 0) {
            break;
        }

        if (output.size() >= OUTPUT_FLUSH_SIZE) {
            if (!user_stats.quiet) {
                cout << output;
            }
            matches += local_matches;
            output.clear();
            local_matches = 0;
        }
    }

    clear_direct_output_context();

    if (!user_stats.quiet) {
        cout << output;
    }
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
    const bool sparse_line_prefilter = before == 0 && after == 0 && !user_stats.invert_match;
    const bool hit_driven_line_scan =
        sparse_line_prefilter && user_stats.max_lines == 0 && !user_stats.invert_match;

    size_t local_matches = 0;
    size_t line_num = 0;
    size_t last_output_line_num = 0;
    unsigned int after_remaining = 0;
    bool stop = false;

    auto stdin_has_binary = [&](const char* data, size_t size) {
        if (user_stats.all_files || std::memchr(data, '\0', size) == nullptr) {
            return false;
        }

        output.clear();
        return true;
    };

    if (user_stats.quiet && (user_stats.invert_match || user_stats.max_lines > 0)) {
        auto process_quiet_line = [&](const char* line_data, size_t line_len) {
            ++line_num;
            const bool contains = contains_match(line_data, line_len, user_stats);
            const bool selected = user_stats.invert_match ? !contains : contains;
            if (selected) {
                local_matches = 1;
                stop = true;
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

            if (stdin_has_binary(buffer.data(), static_cast<size_t>(bytes_read))) {
                return 0;
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
                    process_quiet_line(pending_line.data(), pending_line.size());
                    pending_line.clear();
                } else {
                    process_quiet_line(line_start, line_end - line_start);
                }

                if (stop) {
                    break;
                }

                line_start = line_end + 1;
            }
        }

        if (!stop && !pending_line.empty()) {
            process_quiet_line(pending_line.data(), pending_line.size());
        }

        matches += local_matches;
        return local_matches;
    }

    if (user_stats.quiet) {
        static thread_local std::vector<char> quiet_buffer;
        const size_t overlap_len = pattern_len > 1 ? pattern_len - 1 : 0;
        const size_t buffer_size = LINE_BUFFER_SIZE + overlap_len;
        if (quiet_buffer.size() < buffer_size) {
            quiet_buffer.resize(buffer_size);
        }

        size_t carry_len = 0;
        bool found_match = false;
        while (true) {
            ssize_t bytes_read = ::read(STDIN_FILENO, quiet_buffer.data() + carry_len, LINE_BUFFER_SIZE);

            if (bytes_read <= 0) {
                break;
            }

            const char* data = quiet_buffer.data();
            const size_t size = carry_len + static_cast<size_t>(bytes_read);

            if (stdin_has_binary(data, size)) {
                return 0;
            }

            if (found_match) {
                carry_len = 0;
                continue;
            }

            if (contains_match(data, size, user_stats)) {
                if (user_stats.all_files) {
                    local_matches = 1;
                    break;
                }
                found_match = true;
                carry_len = 0;
                continue;
            }

            if (pattern_len > 1) {
                carry_len = std::min(overlap_len, size);
                std::memmove(quiet_buffer.data(), data + size - carry_len, carry_len);
            }
        }

        if (found_match) {
            local_matches = 1;
        }
        matches += local_matches;
        return local_matches;
    }

    if (user_stats.count_print) {
        auto process_count_line = [&](const char* line_data, size_t line_len) {
            ++line_num;
            const bool contains = contains_match(line_data, line_len, user_stats);
            if (user_stats.invert_match ? !contains : contains) {
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

            if (stdin_has_binary(buffer.data(), static_cast<size_t>(bytes_read))) {
                return 0;
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

        if (local_matches > 0) {
            cout << local_matches << '\n';
        }
        matches += local_matches;

        return local_matches;
    }

    if (user_stats.only_matching) {
        auto flush_if_needed = [&]() {
            if (user_stats.all_files && output.size() >= OUTPUT_FLUSH_SIZE) {
                cout << output;
                output.clear();
            }
        };

        auto emit_occurrence = [&](size_t match_line_num, const char* hit) {
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
                output.append(user_stats.colors.match);
                output.append(hit, pattern_len);
                output.append(RESET);
            } else {
                output.append(hit, pattern_len);
            }
            output.append(user_stats.add_newline ? "\n\n" : "\n");
            ++local_matches;
            flush_if_needed();
        };

        auto process_only_matching_line = [&](const char* line_data, size_t line_len) {
            ++line_num;
            if (pattern_len == 0) {
                ++local_matches;
                if (user_stats.max_lines > 0 && line_num >= user_stats.max_lines) {
                    stop = true;
                }
                return;
            }

            const char* scan_pos = line_data;
            size_t remaining = line_len;

            while (remaining >= pattern_len) {
                const char* hit = find_match(scan_pos, remaining, user_stats);
                if (hit == nullptr) {
                    break;
                }

                emit_occurrence(line_num, hit);
                const size_t advance = static_cast<size_t>(hit - scan_pos) + pattern_len;
                scan_pos += advance;
                remaining -= advance;
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

            if (stdin_has_binary(buffer.data(), static_cast<size_t>(bytes_read))) {
                return 0;
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
                    process_only_matching_line(pending_line.data(), pending_line.size());
                    pending_line.clear();
                } else {
                    process_only_matching_line(line_start, line_end - line_start);
                }

                if (stop) {
                    break;
                }

                line_start = line_end + 1;
            }
        }

        if (!stop && !pending_line.empty()) {
            process_only_matching_line(pending_line.data(), pending_line.size());
        }

        cout << output;
        matches += local_matches;
        return local_matches;
    }

    auto flush_if_needed = [&]() {
        if (user_stats.all_files && output.size() >= OUTPUT_FLUSH_SIZE) {
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
        last_output_line_num = match_line_num;
        flush_if_needed();
    };

    auto process_line = [&](const char* line_data, size_t line_len) {
        ++line_num;
        const char* hit = nullptr;

        hit = find_match(line_data, line_len, user_stats);

        const bool selected = user_stats.invert_match ? hit == nullptr : hit != nullptr;

        if (selected) {
            if (before > 0) {
                size_t context_line_num = line_num - prev_lines.size();
                for (const auto& pline : prev_lines) {
                    if (context_line_num > last_output_line_num) {
                        output.append(pline);
                        output.push_back('\n');
                        last_output_line_num = context_line_num;
                        flush_if_needed();
                    }
                    ++context_line_num;
                }
            }

            emit_match(line_data, line_len, line_num, hit);
            after_remaining = after;
        }
        else if (after_remaining > 0) {
            if (line_num > last_output_line_num) {
                output.append(line_data, line_len);
                output.push_back('\n');
                last_output_line_num = line_num;
                flush_if_needed();
            }
            --after_remaining;
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

        if (stdin_has_binary(buffer.data(), static_cast<size_t>(bytes_read))) {
            return 0;
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
                const char* hit = find_match(
                    scan_pos,
                    chunk_end - scan_pos,
                    user_stats
                );

                if (hit == nullptr) {
                    break;
                }

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
             !contains_match(chunk_start, static_cast<size_t>(bytes_read), user_stats))) {
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
