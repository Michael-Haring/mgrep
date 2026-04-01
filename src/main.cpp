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
#include <fstream>
#include <vector>

using std::cout, std::string;

constexpr int SUCCESS = 0;
constexpr int FAILURE = 1;

constexpr char* OPTIONS = (char*)"vhrcnls";
constexpr char* ORANGE = (char*)"\033[38;5;202m";
constexpr char* YELLOW = (char*)"\033[38;5;220m";
constexpr char* LIGHTGREEN = (char*)"\033[38;5;120m";
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
};


void printHelp(char* file_name);
void jump_into_dir(std::filesystem::path root, ThreadPool& tp, UserOptions& user_stats);
void read_file(const std::filesystem::path& path, ThreadPool& tp, UserOptions& user_stats);
bool check_user_root(std::filesystem::path& root);


void printHelp(char* file_name)
{
    cout << "\033[38;5;202mPrinting help for program: " << file_name << "\n";
    cout << "\t-h\t\tPrints this marvalous help test\n";
    cout << "\t-v\t\tEnables verbose output \n";
    cout << "\t-r\t\tRecursively search all dirs below dir provided\033[0m\n";
    cout << "mgrep is a groundbreaking new program brought to you by MNU\n";
    cout << "\tUsage:\tmgrep -[OPTIONS] \"pattern\" root_dir\n";
    cout << "grep -r \"main\" .\n";
    cout << "Exactly like grep, but better because its multi-threaded\n";
}

void jump_into_dir(std::filesystem::path root, ThreadPool& tp, UserOptions& user_stats)
{
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (entry.is_regular_file()) {
            tp.push_task([path = entry.path(), &tp, &user_stats]() {
                read_file(path, tp, user_stats);
            });
        }
        else if (user_stats.recursive_mode && entry.is_directory()) {
            jump_into_dir(entry, tp, user_stats);
        }
    }
}

void read_file(const std::filesystem::path& path, ThreadPool& tp, UserOptions& user_stats)
{

    std::ifstream file(path);
    if (!file) {
        std::cerr << "ERROR -\t" << path << " not found\n";
        return;
    }

    std::string line;
    size_t line_num = 0;
    while (std::getline(file, line)) {
        ++line_num;

        if (line.find(user_stats.pattern) != std::string::npos) {
            std::lock_guard<std::mutex> lock(tp.m_cout_mtx);
            if (user_stats.cool_colors) {
                cout << path.parent_path().string() << "/" << ORANGE << path.filename().string()
                    << RESET;
                if (user_stats.line_number_print) {
                    cout << YELLOW << "\tLine: " << line_num << RESET;
                }
                if (user_stats.source_print) {
                    cout << "\n" << LIGHTGREEN << line << RESET;
                }
            } else {
                cout << path.parent_path().string() << "/" << path.filename().string();
                if (user_stats.line_number_print) {
                    cout << "\tLine: " << line_num;
                }
                if (user_stats.source_print) {
                    cout << "\n" << line << "\n";
                }
            }
            if (user_stats.add_newline) {
                cout << "\n\n";
            } else {
                cout << "\n";
            }
            ++matches;
        }
    }
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
            }
        }
    }
    //untested
    if (argv[optind]) {
        user_stats.pattern = argv[optind];
        ++optind;
    }

    for (int i = optind; i < argc; ++i) {
        std::filesystem::path root = argv[i];
        if (check_user_root(root)) {
            jump_into_dir(root, tp, user_stats);
        } else break;

    }


    tp.wait_for_all();


    if (user_stats.is_verbose) {
        cout << "Searched " << tp.count_tasks_completed() << " files and found " << matches << " matches\n";
    }


    return 0;
}
