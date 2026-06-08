#include "traversal.hpp"

#include "ignore.hpp"
#include "output.hpp"

#include <dirent.h>
#include <iostream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using std::cout;

void flush_output(ThreadPool& tp, std::string& output, size_t& local_matches)
{
    if (output.empty() && local_matches == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(tp.m_cout_mtx);
    cout << output;
    matches += local_matches;
    output.clear();
    local_matches = 0;
}

void push_file_batch(
    std::vector<std::string>& batch,
    ThreadPool& tp,
    UserOptions& user_stats,
    ReadFileFn read_file
) {
    if (batch.empty()) {
        return;
    }

    tp.push_task([files = std::move(batch), &tp, &user_stats, read_file]() {
        std::string output;
        output.reserve(8192);
        size_t local_matches = 0;

        for (const auto& path : files) {
            local_matches += read_file(path, user_stats, output);

            if (output.size() >= OUTPUT_FLUSH_SIZE) {
                flush_output(tp, output, local_matches);
            }
        }

        flush_output(tp, output, local_matches);
    });

    batch.clear();
    batch.reserve(FILE_BATCH_SIZE);
}

void add_search_path(SearchWork& work, const std::string& path)
{
    if (!work.tp && work.small_paths.size() < SINGLE_THREAD_FILE_LIMIT) {
        work.small_paths.push_back(path);
        return;
    }

    if (!work.tp) {
        work.tp = std::make_unique<ThreadPool>();

        for (auto& small_path : work.small_paths) {
            work.batch.push_back(std::move(small_path));

            if (work.batch.size() >= FILE_BATCH_SIZE) {
                push_file_batch(work.batch, *work.tp, work.user_stats, work.read_file);
            }
        }

        work.small_paths.clear();
    }

    work.batch.push_back(path);

    if (work.batch.size() >= FILE_BATCH_SIZE) {
        push_file_batch(work.batch, *work.tp, work.user_stats, work.read_file);
    }
}

size_t finish_search(SearchWork& work)
{
    if (!work.tp) {
        return search_files_single_thread(work.small_paths, work.user_stats, work.read_file);
    }

    push_file_batch(work.batch, *work.tp, work.user_stats, work.read_file);
    work.tp->wait_for_all();

    return work.tp->count_tasks_completed();
}

void collect_search_files(
    const std::string& root,
    SearchWork& work
) {
    struct stat st{};

    if (::stat(root.c_str(), &st) != 0) {
        return;
    }

    if (S_ISREG(st.st_mode)) {
        const size_t name_pos = filename_pos(root);
        const std::string_view name(root.data() + name_pos, root.size() - name_pos);

        if (work.user_stats.all_files ||
            (!should_skip_file(name) && !should_ignore_file(name, work.user_stats.ignore_rules))) {
            add_search_path(work, root);
        }
        return;
    }

    if (!S_ISDIR(st.st_mode)) {
        return;
    }

    std::string path = root;
    if (work.user_stats.recursive_mode) {
        collect_search_files_recursive_parallel(path, work);
    } else {
        collect_search_files_recursive(path, work);
    }
}

void collect_search_files_recursive(
    std::string& root,
    SearchWork& work
) {
    DIR* dir = ::opendir(root.c_str());
    if (dir == nullptr) {
        return;
    }

    const size_t root_len = root.size();
    const bool add_separator = !root.empty() && root.back() != '/';
    const int dir_fd = ::dirfd(dir);

    while (dirent* entry = ::readdir(dir)) {
        const char* name = entry->d_name;

        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }

        unsigned char type = entry->d_type;

        if (type == DT_LNK) {
            continue;
        }

        if (type == DT_UNKNOWN) {
            struct stat st{};
            if (::fstatat(dir_fd, name, &st, 0) != 0) {
                continue;
            }

            if (S_ISREG(st.st_mode)) {
                type = DT_REG;
            } else if (S_ISDIR(st.st_mode)) {
                type = DT_DIR;
            }
        }

        if (type == DT_REG) {

            if (!work.user_stats.all_files &&
                (should_skip_file(name) || should_ignore_file(name, work.user_stats.ignore_rules))) {
                continue;
            }

            root.resize(root_len);
            if (add_separator) {
                root.push_back('/');
            }
            root.append(name);

            add_search_path(work, root);
        }
        else if (work.user_stats.recursive_mode && type == DT_DIR) {
            if (!work.user_stats.all_files &&
                (should_skip_dir(name) || should_ignore_dir(name, work.user_stats.ignore_rules))) {
                continue;
            }

            root.resize(root_len);
            if (add_separator) {
                root.push_back('/');
            }
            root.append(name);

            collect_search_files_recursive(root, work);
        }
    }

    root.resize(root_len);
    ::closedir(dir);
}

void collect_search_files_one_dir(
    std::string& root,
    DirectoryTraversalWork& traversal
) {
    DIR* dir = ::opendir(root.c_str());
    if (dir == nullptr) {
        return;
    }

    const size_t root_len = root.size();
    const bool add_separator = !root.empty() && root.back() != '/';
    const int dir_fd = ::dirfd(dir);
    std::vector<std::string> found_files;
    std::vector<std::string> child_dirs;
    found_files.reserve(FILE_BATCH_SIZE);

    while (dirent* entry = ::readdir(dir)) {
        const char* name = entry->d_name;

        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }

        unsigned char type = entry->d_type;

        if (type == DT_LNK) {
            continue;
        }

        if (type == DT_UNKNOWN) {
            struct stat st{};
            if (::fstatat(dir_fd, name, &st, 0) != 0) {
                continue;
            }

            if (S_ISREG(st.st_mode)) {
                type = DT_REG;
            } else if (S_ISDIR(st.st_mode)) {
                type = DT_DIR;
            }
        }

        if (type == DT_REG) {

            if (!traversal.search.user_stats.all_files &&
                (should_skip_file(name) ||
                 should_ignore_file(name, traversal.search.user_stats.ignore_rules))) {
                continue;
            }

            root.resize(root_len);
            if (add_separator) {
                root.push_back('/');
            }
            root.append(name);

            found_files.push_back(root);
        }
        else if (type == DT_DIR) {
            if (!traversal.search.user_stats.all_files &&
                (should_skip_dir(name) ||
                 should_ignore_dir(name, traversal.search.user_stats.ignore_rules))) {
                continue;
            }

            root.resize(root_len);
            if (add_separator) {
                root.push_back('/');
            }
            root.append(name);

            child_dirs.push_back(root);
        }
    }

    ::closedir(dir);

    if (!found_files.empty()) {
        std::lock_guard<std::mutex> lock(traversal.search_mtx);
        for (const auto& file : found_files) {
            add_search_path(traversal.search, file);
        }
    }

    if (!child_dirs.empty()) {
        std::lock_guard<std::mutex> lock(traversal.dirs_mtx);
        for (auto& child : child_dirs) {
            traversal.dirs.push(std::move(child));
        }
        traversal.cv.notify_all();
    }

    root.resize(root_len);
}

void traverse_dir_worker(DirectoryTraversalWork& traversal)
{
    while (true) {
        std::string dir_path;

        {
            std::unique_lock<std::mutex> lock(traversal.dirs_mtx);
            traversal.cv.wait(lock, [&] {
                return traversal.done || !traversal.dirs.empty();
            });

            if (traversal.done && traversal.dirs.empty()) {
                return;
            }

            dir_path = std::move(traversal.dirs.front());
            traversal.dirs.pop();
            ++traversal.active_dirs;
        }

        collect_search_files_one_dir(dir_path, traversal);

        {
            std::lock_guard<std::mutex> lock(traversal.dirs_mtx);
            --traversal.active_dirs;
            if (traversal.active_dirs == 0 && traversal.dirs.empty()) {
                traversal.done = true;
                traversal.cv.notify_all();
            }
        }
    }
}

void collect_search_files_recursive_parallel(
    std::string& root,
    SearchWork& work
) {
    const unsigned int hardware_threads = std::thread::hardware_concurrency();
    size_t num_walkers = hardware_threads > 0 ? hardware_threads / 4 : 1;
    if (num_walkers < 2) {
        num_walkers = 2;
    }
    if (num_walkers > 4) {
        num_walkers = 4;
    }

    DirectoryTraversalWork traversal(work);
    traversal.dirs.push(root);

    std::vector<std::thread> walkers;
    walkers.reserve(num_walkers);
    for (size_t i = 0; i < num_walkers; ++i) {
        walkers.emplace_back(traverse_dir_worker, std::ref(traversal));
    }

    for (auto& walker : walkers) {
        walker.join();
    }
}
