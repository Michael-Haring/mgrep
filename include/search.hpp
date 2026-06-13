#pragma once

#include "options.hpp"

#include <cstddef>
#include <string>
#include <vector>

inline constexpr size_t OUTPUT_FLUSH_SIZE = 1024 * 1024;

extern size_t matches;

using ReadFileFn = size_t (*)(const std::string&, UserOptions&, std::string&);

struct DirectOutputPiece {
    const char* data;
    size_t size;
};

using DirectOutputFn = void (*)(
    void* context,
    std::string& output,
    const DirectOutputPiece* pieces,
    size_t piece_count
);

void set_direct_output_context(DirectOutputFn fn, void* context);
void clear_direct_output_context();

size_t read_file_fast(const std::string& path, UserOptions& user_stats, std::string& output);
size_t read_file_line_options(const std::string& path, UserOptions& user_stats, std::string& output);
size_t read_file_count(const std::string& path, UserOptions& user_stats, std::string& output);
size_t read_file_only_matching(const std::string& path, UserOptions& user_stats, std::string& output);
ReadFileFn choose_read_file(const UserOptions& user_stats);
size_t search_files_single_thread(
    const std::vector<std::string>& paths,
    UserOptions& user_stats,
    ReadFileFn read_file
);
size_t search_stdin(UserOptions& user_stats);
int exit_code_from_matches();
