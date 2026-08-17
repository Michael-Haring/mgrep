#include "traversal.hpp"

#include "ignore.hpp"
#include "output.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <iostream>
#include <iterator>
#include <sys/uio.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using std::cout;

namespace {

void report_directory_error(const std::string& path)
{
    const int error = errno;
    search_error.store(true, std::memory_order_relaxed);
    std::cerr << "ERROR: could not read directory: " << path
              << ": " << std::strerror(error) << "\n";
}

void write_direct_stdout(std::string& output, const DirectOutputPiece* pieces, size_t piece_count)
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

void direct_output_threaded(
    void* context,
    std::string& output,
    const DirectOutputPiece* pieces,
    size_t piece_count
)
{
    auto& tp = *static_cast<ThreadPool*>(context);
    std::lock_guard<std::mutex> lock(tp.m_cout_mtx);
    cout.flush();
    write_direct_stdout(output, pieces, piece_count);
}

void append_list_file(SearchWork& work, const std::string& path)
{
    if (work.user_stats.quiet) {
        return;
    }

    std::lock_guard<std::mutex> lock(work.list_mtx);
    if (work.user_stats.cool_colors) {
        work.list_output += work.user_stats.colors.path;
        work.list_output += path;
        work.list_output += RESET;
    } else {
        work.list_output += path;
    }
    work.list_output.push_back(work.user_stats.null_output ? '\0' : '\n');

    if (work.list_output.size() >= OUTPUT_FLUSH_SIZE) {
        cout << work.list_output;
        work.list_output.clear();
    }
}

void flush_list_files(SearchWork& work)
{
    if (work.list_output.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(work.list_mtx);
    cout << work.list_output;
    work.list_output.clear();
}

} // namespace

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
    ReadFileFn read_file,
    std::atomic<bool>& stop_requested
) {
    if (batch.empty()) {
        return;
    }

    tp.push_task([files = std::move(batch), &tp, &user_stats, read_file, &stop_requested]() {
        std::string output;
        output.reserve(8192);
        size_t local_matches = 0;
        set_direct_output_context(direct_output_threaded, &tp);

        for (const auto& path : files) {
            if (user_stats.quiet && stop_requested.load(std::memory_order_relaxed)) {
                break;
            }

            local_matches += read_file(path, user_stats, output);

            if (user_stats.quiet && local_matches > 0) {
                stop_requested.store(true, std::memory_order_relaxed);
                break;
            }

            if (output.size() >= OUTPUT_FLUSH_SIZE) {
                flush_output(tp, output, local_matches);
            }
        }

        clear_direct_output_context();
        flush_output(tp, output, local_matches);
    });

    batch.clear();
    batch.reserve(FILE_BATCH_SIZE);
}

void add_search_path(SearchWork& work, const std::string& path)
{
    if (work.user_stats.list_files) {
        append_list_file(work, path);
        return;
    }

    if (work.user_stats.quiet && work.stop_requested.load(std::memory_order_relaxed)) {
        return;
    }

    const size_t single_thread_limit =
        work.user_stats.recursive_mode ? FILE_BATCH_SIZE : SINGLE_THREAD_FILE_LIMIT;

    if (!work.tp && work.small_paths.size() < single_thread_limit) {
        work.small_paths.push_back(path);
        return;
    }

    if (!work.tp) {
        work.tp = std::make_unique<ThreadPool>();

        for (auto& small_path : work.small_paths) {
            work.batch.push_back(std::move(small_path));

            if (work.batch.size() >= FILE_BATCH_SIZE) {
                push_file_batch(
                    work.batch,
                    *work.tp,
                    work.user_stats,
                    work.read_file,
                    work.stop_requested
                );
            }
        }

        work.small_paths.clear();
    }

    work.batch.push_back(path);

    if (work.batch.size() >= FILE_BATCH_SIZE) {
        push_file_batch(work.batch, *work.tp, work.user_stats, work.read_file, work.stop_requested);
    }
}

void add_search_paths(SearchWork& work, std::vector<std::string>& paths)
{
    if (work.user_stats.list_files) {
        for (const auto& path : paths) {
            append_list_file(work, path);
        }
        paths.clear();
        return;
    }

    if (paths.empty() ||
        (work.user_stats.quiet && work.stop_requested.load(std::memory_order_relaxed))) {
        return;
    }

    const size_t single_thread_limit =
        work.user_stats.recursive_mode ? FILE_BATCH_SIZE : SINGLE_THREAD_FILE_LIMIT;

    auto start_thread_pool = [&]() {
        work.tp = std::make_unique<ThreadPool>();

        for (auto& small_path : work.small_paths) {
            work.batch.push_back(std::move(small_path));

            if (work.batch.size() >= FILE_BATCH_SIZE) {
                push_file_batch(
                    work.batch,
                    *work.tp,
                    work.user_stats,
                    work.read_file,
                    work.stop_requested
                );
            }
        }

        work.small_paths.clear();
    };

    if (!work.tp && work.small_paths.size() + paths.size() <= single_thread_limit) {
        work.small_paths.insert(
            work.small_paths.end(),
            std::make_move_iterator(paths.begin()),
            std::make_move_iterator(paths.end())
        );
        paths.clear();
        return;
    }

    if (!work.tp) {
        start_thread_pool();
    }

    for (auto& path : paths) {
        if (work.user_stats.quiet && work.stop_requested.load(std::memory_order_relaxed)) {
            break;
        }

        work.batch.push_back(std::move(path));

        if (work.batch.size() >= FILE_BATCH_SIZE) {
            push_file_batch(work.batch, *work.tp, work.user_stats, work.read_file, work.stop_requested);
        }
    }

    paths.clear();
}

size_t finish_search(SearchWork& work)
{
    if (work.user_stats.list_files) {
        flush_list_files(work);
        return 0;
    }

    if (!work.tp) {
        return search_files_single_thread(work.small_paths, work.user_stats, work.read_file);
    }

    push_file_batch(work.batch, *work.tp, work.user_stats, work.read_file, work.stop_requested);
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
        if (path_filter_allows(root, work.user_stats)) {
            add_search_path(work, root);
        }
        return;
    }

    if (!S_ISDIR(st.st_mode)) {
        return;
    }

    if (work.user_stats.max_depth == 0) {
        return;
    }

    std::string path = root;
    if (work.user_stats.recursive_mode) {
        collect_search_files_recursive_parallel(path, work);
    } else {
        collect_search_files_recursive(path, work, 0);
    }
}

void collect_search_files_recursive(
    std::string& root,
    SearchWork& work,
    unsigned int depth
) {
    DIR* dir = ::opendir(root.c_str());
    if (dir == nullptr) {
        report_directory_error(root);
        return;
    }

    const size_t root_len = root.size();
    const bool add_separator = !root.empty() && root.back() != '/';
    const int dir_fd = ::dirfd(dir);

    while (dirent* entry = ::readdir(dir)) {
        if (work.user_stats.quiet && work.stop_requested.load(std::memory_order_relaxed)) {
            break;
        }

        const char* name = entry->d_name;

        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }

        unsigned char type = entry->d_type;

        if (type == DT_LNK) {
            struct stat st{};
            if (::fstatat(dir_fd, name, &st, 0) != 0 || !S_ISREG(st.st_mode)) {
                continue;
            }
            type = DT_REG;
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

            if (!path_filter_allows(root, work.user_stats)) {
                continue;
            }

            add_search_path(work, root);
        }
        else if (work.user_stats.recursive_mode &&
                 depth + 1 < work.user_stats.max_depth &&
                 type == DT_DIR) {
            if (!work.user_stats.all_files &&
                (should_skip_dir(name) || should_ignore_dir(name, work.user_stats.ignore_rules))) {
                continue;
            }

            root.resize(root_len);
            if (add_separator) {
                root.push_back('/');
            }
            root.append(name);

            if (exclude_glob_matches_directory(root, work.user_stats)) {
                continue;
            }

            collect_search_files_recursive(root, work, depth + 1);
        }
    }

    root.resize(root_len);
    ::closedir(dir);
}

void collect_search_files_one_dir(
    std::string& root,
    unsigned int depth,
    DirectoryTraversalWork& traversal
) {
    DIR* dir = ::opendir(root.c_str());
    if (dir == nullptr) {
        report_directory_error(root);
        return;
    }

    const size_t root_len = root.size();
    const bool add_separator = !root.empty() && root.back() != '/';
    const int dir_fd = ::dirfd(dir);
    std::vector<std::string> found_files;
    std::vector<std::string> child_dirs;
    found_files.reserve(FILE_BATCH_SIZE);

    while (dirent* entry = ::readdir(dir)) {
        if (traversal.search.user_stats.quiet &&
            traversal.search.stop_requested.load(std::memory_order_relaxed)) {
            break;
        }

        const char* name = entry->d_name;

        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }

        unsigned char type = entry->d_type;

        if (type == DT_LNK) {
            struct stat st{};
            if (::fstatat(dir_fd, name, &st, 0) != 0 || !S_ISREG(st.st_mode)) {
                continue;
            }
            type = DT_REG;
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

            if (!path_filter_allows(root, traversal.search.user_stats)) {
                continue;
            }

            found_files.push_back(root);
        }
        else if (depth + 1 < traversal.search.user_stats.max_depth && type == DT_DIR) {
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

            if (exclude_glob_matches_directory(root, traversal.search.user_stats)) {
                continue;
            }

            child_dirs.push_back(root);
        }
    }

    ::closedir(dir);

    if (!found_files.empty()) {
        std::lock_guard<std::mutex> lock(traversal.search_mtx);
        add_search_paths(traversal.search, found_files);
    }

    if (!child_dirs.empty() &&
        !(traversal.search.user_stats.quiet &&
          traversal.search.stop_requested.load(std::memory_order_relaxed))) {
        std::lock_guard<std::mutex> lock(traversal.dirs_mtx);
        for (auto& child : child_dirs) {
            traversal.dirs.emplace(std::move(child), depth + 1);
        }
        traversal.cv.notify_all();
    }

    root.resize(root_len);
}

void traverse_dir_worker(DirectoryTraversalWork& traversal)
{
    while (true) {
        std::string dir_path;
        unsigned int depth = 0;

        {
            std::unique_lock<std::mutex> lock(traversal.dirs_mtx);
            traversal.cv.wait(lock, [&] {
                return traversal.done || !traversal.dirs.empty() ||
                    (traversal.search.user_stats.quiet &&
                     traversal.search.stop_requested.load(std::memory_order_relaxed));
            });

            if (traversal.search.user_stats.quiet &&
                traversal.search.stop_requested.load(std::memory_order_relaxed)) {
                traversal.done = true;
                traversal.cv.notify_all();
                return;
            }

            if (traversal.done && traversal.dirs.empty()) {
                return;
            }

            dir_path = std::move(traversal.dirs.front().first);
            depth = traversal.dirs.front().second;
            traversal.dirs.pop();
            ++traversal.active_dirs;
        }

        collect_search_files_one_dir(dir_path, depth, traversal);

        {
            std::lock_guard<std::mutex> lock(traversal.dirs_mtx);
            --traversal.active_dirs;
            if ((traversal.search.user_stats.quiet &&
                 traversal.search.stop_requested.load(std::memory_order_relaxed)) ||
                (traversal.active_dirs == 0 && traversal.dirs.empty())) {
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
    traversal.dirs.emplace(root, 0);

    std::vector<std::thread> walkers;
    walkers.reserve(num_walkers);
    for (size_t i = 0; i < num_walkers; ++i) {
        walkers.emplace_back(traverse_dir_worker, std::ref(traversal));
    }

    for (auto& walker : walkers) {
        walker.join();
    }
}
