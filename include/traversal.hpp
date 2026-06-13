#pragma once

#include "search.hpp"
#include "threads.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

inline constexpr size_t FILE_BATCH_SIZE = 32;
inline constexpr size_t SINGLE_THREAD_FILE_LIMIT = 80;

struct SearchWork {
    explicit SearchWork(UserOptions& options)
        : user_stats(options)
    {
        small_paths.reserve(SINGLE_THREAD_FILE_LIMIT);
        batch.reserve(FILE_BATCH_SIZE);
    }

    UserOptions& user_stats;
    std::vector<std::string> small_paths;
    std::vector<std::string> batch;
    std::unique_ptr<ThreadPool> tp;
    ReadFileFn read_file = nullptr;
    std::atomic<bool> stop_requested{false};
};

struct DirectoryTraversalWork {
    explicit DirectoryTraversalWork(SearchWork& search_work)
        : search(search_work)
    {}

    SearchWork& search;
    std::queue<std::string> dirs;
    std::mutex dirs_mtx;
    std::mutex search_mtx;
    std::condition_variable cv;
    size_t active_dirs = 0;
    bool done = false;
};

void push_file_batch(
    std::vector<std::string>& batch,
    ThreadPool& tp,
    UserOptions& user_stats,
    ReadFileFn read_file,
    std::atomic<bool>& stop_requested
);
void collect_search_files_recursive(std::string& root, SearchWork& work);
void collect_search_files_recursive_parallel(std::string& root, SearchWork& work);
void traverse_dir_worker(DirectoryTraversalWork& traversal);
void collect_search_files_one_dir(std::string& root, DirectoryTraversalWork& traversal);
void collect_search_files(const std::string& root, SearchWork& work);
void add_search_path(SearchWork& work, const std::string& path);
void add_search_paths(SearchWork& work, std::vector<std::string>& paths);
size_t finish_search(SearchWork& work);
