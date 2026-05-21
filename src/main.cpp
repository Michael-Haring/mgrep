/*
* @file     threads.cpp
* @author   Michael Haring
* @date     3/23/26
*
*
* optimize scopes
* add errors for invalid path provided
*
*
* change options to a struct, apply changes
*
* */

#include "threads.hpp"
#include <mutex>
#include <iostream>
#include <getopt.h>
#include <cmath>
#include <filesystem>
#include <vector>
#include <unordered_set>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sstream>

using std::cout, std::string;

constexpr int SUCCESS = 0;
constexpr int FAILURE = 1;

constexpr size_t FILE_BATCH_SIZE = 64;

constexpr char* OPTIONS = (char*)"vhrcnlsaB:A:m:";
constexpr char* ORANGE = (char*)"\033[38;5;202m";
constexpr char* YELLOW = (char*)"\033[38;5;220m";
constexpr char* LIGHTGREEN = (char*)"\033[38;5;118m";
constexpr char* RESET = (char*)"\033[0m";
size_t matches = 0;


struct UserOptions {
    string pattern = "";
    bool recursive_mode = false;
    bool is_verbose = false;
    bool cool_colors = false;
    bool source_print = false;
    bool line_number_print = false;
    bool add_newline = false;
    bool all_files = false;
    unsigned int print_before_source = 0;
    unsigned int print_after_source = 0;
    unsigned int max_lines = 0;
};

void jump_into_dir(
    const std::filesystem::path& root,
    ThreadPool& tp,
    UserOptions& user_stats,
    std::vector<std::filesystem::path>& batch
);
void printHelp(char* file_name);
void read_file(const std::filesystem::path& path, ThreadPool& tp, UserOptions& user_stats);
bool check_user_root(std::filesystem::path& root);
static const std::unordered_set<std::string_view> skipped_exts = {
    ".so", ".a", ".o", ".exe", ".dll",
    ".bin", ".db", ".cmake",
    ".png", ".jpg", ".pdf", ".pyc"
};


void push_file_batch(
    std::vector<std::filesystem::path>& batch,
    ThreadPool& tp,
    UserOptions& user_stats
) {
    if (batch.empty()) {
        return;
    }

    tp.push_task([files = std::move(batch), &tp, &user_stats]() {
        for (const auto& path : files) {
            read_file(path, tp, user_stats);
        }
    });

    batch.clear();
    batch.reserve(FILE_BATCH_SIZE);
}

bool should_skip_file(const std::filesystem::path& path)
{
    const std::string ext = path.extension().string();
    return ext.empty() || skipped_exts.find(ext) != skipped_exts.end();
}

void printHelp(char* file_name)
{
    cout << ORANGE << "Printing help for program: " << file_name << "\n";
    cout << "\t-h\tPrints this marvalous help test\n";
    cout << "\t-v\tEnables verbose output \n";
    cout << "\t-r\tRecursively search all dirs below dir provided\n";
    cout << "\t-c\tEnables colorful output\n";
    cout << "\t-n\tPrints an additional newline between pattern finds\n";
    cout << "\t-l\tPrints line number in file pattern is found in\n";
    cout << "\t-s\tPrints line of source code the pattern was found in\n";
    cout << "\t-a\tSearches all files.\n";
    cout << "\t\tFile extensions that are not read by default:\n";
    cout << "\t\t.so - .a - .o - .exe - .dll - .17git - .bin - \n";
    cout << "\t\t.db - . - .git - .cmake\n";

    cout << "\nmgrep is a groundbreaking new program brought to you by MNU\n";
    cout << "\tUsage:\tmgrep -[OPTIONS] \"pattern\" root_dir\n";
    cout << "\tgrep -r \"main\" .\n";
    cout << "Exactly like grep, but better because I made it.\n";
}


void jump_into_dir(
    const std::filesystem::path& root,
    ThreadPool& tp,
    UserOptions& user_stats,
    std::vector<std::filesystem::path>& batch
) {
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (entry.is_regular_file()) {
            auto path = entry.path();

            if (!user_stats.all_files && should_skip_file(path)) {
                continue;
            }

            batch.push_back(std::move(path));

            if (batch.size() >= FILE_BATCH_SIZE) {
                push_file_batch(batch, tp, user_stats);
            }
        }
        else if (user_stats.recursive_mode && entry.is_directory()) {
            jump_into_dir(entry.path(), tp, user_stats, batch);
        }
    }
}


void read_file(const std::filesystem::path& path, ThreadPool& tp, UserOptions& user_stats)
{
        //  BEST VERSION THUS FAR

    constexpr size_t BUFFER_SIZE = 64 * 1024;

    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
        return;
    }

    std::array<char, BUFFER_SIZE> buffer{};
    std::string pending_line;
    pending_line.reserve(4096);

    std::deque<std::string> prev_lines;

    const unsigned int before = user_stats.print_before_source;
    const unsigned int after = user_stats.print_after_source;
    unsigned int after_remaining = 0;

    const std::string printable_path =
        path.parent_path().string() + "/" + path.filename().string();

    size_t line_num = 0;

    auto process_line = [&](const char* line_data, size_t line_len) {
        ++line_num;

        bool found = false;

        if (line_len >= user_stats.pattern.size()) {
            const char first = user_stats.pattern[0];
            const size_t pattern_len = user_stats.pattern.size();

            const char* search_pos = line_data;
            const char* search_end = line_data + line_len - pattern_len + 1;

            while (search_pos < search_end) {
                const void* hit = std::memchr(search_pos, first, search_end - search_pos);

                if (!hit) {
                    break;
                }

                const char* candidate = static_cast<const char*>(hit);

                if (std::memcmp(candidate, user_stats.pattern.data(), pattern_len) == 0) {
                    found = true;
                    break;
                }
                search_pos = candidate + 1;
            }
        }

        if (found) {
            std::ostringstream out;

            if (before > 0) {
                for (const auto& pline : prev_lines) {
                    out << pline << '\n';
                }
            }

            if (user_stats.cool_colors) {
                out << path.parent_path().string() << "/"
                    << ORANGE << path.filename().string()
                    << RESET;

                if (user_stats.line_number_print) {
                    out << YELLOW << "<line:" << line_num << ">" << RESET;
                }

                if (user_stats.source_print) {
                    out << LIGHTGREEN << "-->";
                    out.write(line_data, line_len);
                    out << RESET;
                }
            } else {
                out << printable_path;

                if (user_stats.line_number_print) {
                    out << "<line:" << line_num << ">";
                }

                if (user_stats.source_print) {
                    out << "-->";
                    out.write(line_data, line_len);
                }
            }

            out << (user_stats.add_newline ? "\n\n" : "\n");

            {
                std::lock_guard<std::mutex> lock(tp.m_cout_mtx);
                cout << out.str();
            }

            ++matches;
            after_remaining = after;
        }
        else if (after_remaining > 0) {
            std::lock_guard<std::mutex> lock(tp.m_cout_mtx);
            cout.write(line_data, line_len);
            cout << '\n';
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
}


bool check_user_root(std::filesystem::path& root)
{
    std::error_code ec;

    if (!std::filesystem::exists(root, ec)) {
        std::cerr << "ERROR: path does not exist: " << root.string() << "\n";
        return false;
    }

    return true;
}


int main(int argc, char* argv[])
{
    ThreadPool tp;
    std::queue<string> file_names;
    string original_dir = "";
    UserOptions user_stats;

    {
        int opt = 0;
        while ((opt = getopt(argc, argv, OPTIONS)) != -1) {
            switch(opt) {
                case 'v':
                    user_stats.is_verbose = true;
                    break;
                case 'h':
                    printHelp(argv[0]);
                    return SUCCESS;
                    break;
                case 'r':
                    user_stats.recursive_mode = true;
                    break;
                case 'c':
                    user_stats.cool_colors = true;
                    break;
                case 'n':
                    user_stats.add_newline = true;
                    break;
                case 'l':
                    user_stats.line_number_print = true;
                    break;
                case 's':
                    user_stats.source_print = true;
                    break;
                case 'a':
                    user_stats.all_files = true;
                    break;
                case 'B':
                    user_stats.print_before_source = atoi(optarg);
                    break;
                case 'A':
                    user_stats.print_after_source = atoi(optarg);
                    break;
                case 'm':
                    user_stats.max_lines = atoi(optarg);
                    break;
            }
        }
    }
    //untested
    if (argv[optind]) {
        user_stats.pattern = argv[optind];
        ++optind;
    }
    std::vector<std::filesystem::path> batch;
    batch.reserve(FILE_BATCH_SIZE);

    for (int i = optind; i < argc; ++i) {
        std::filesystem::path root = argv[i];

        if (check_user_root(root)) {
            jump_into_dir(root, tp, user_stats, batch);
        } else {
            break;
        }
    }

    push_file_batch(batch, tp, user_stats);
    tp.wait_for_all();

    if (user_stats.is_verbose) {
        cout << "Searched " << tp.count_tasks_completed() << " files and found " << matches << " matches\n";
    }


    return 0;
}
