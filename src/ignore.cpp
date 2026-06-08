#include "ignore.hpp"

#include <cstdlib>
#include <fstream>

std::string_view trim_ignore_line(std::string_view line)
{
    while (!line.empty() && static_cast<unsigned char>(line.front()) <= ' ') {
        line.remove_prefix(1);
    }

    while (!line.empty() && static_cast<unsigned char>(line.back()) <= ' ') {
        line.remove_suffix(1);
    }

    return line;
}

std::string_view basename_ignore_rule(std::string_view rule)
{
    while (!rule.empty() && rule.front() == '/') {
        rule.remove_prefix(1);
    }

    const size_t slash_pos = rule.find_last_of('/');
    if (slash_pos != std::string_view::npos) {
        rule.remove_prefix(slash_pos + 1);
    }

    return rule;
}

void load_home_ignore(IgnoreRules& rules)
{
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        return;
    }

    std::string path = home;
    if (!path.empty() && path.back() != '/') {
        path.push_back('/');
    }
    path.append(".ignore");

    std::ifstream input(path);
    if (!input) {
        return;
    }

    rules.exact_names.reserve(32);
    rules.dir_names.reserve(16);
    rules.suffixes.reserve(16);

    std::string line;
    while (std::getline(input, line)) {
        std::string_view rule = trim_ignore_line(line);
        if (rule.empty() || rule.front() == '#' || rule.front() == '!') {
            continue;
        }

        const bool dir_only = rule.back() == '/';
        while (!rule.empty() && rule.back() == '/') {
            rule.remove_suffix(1);
        }

        rule = basename_ignore_rule(rule);
        if (rule.empty() || rule.find_first_of("?[") != std::string_view::npos) {
            continue;
        }

        if (rule.size() > 1 && rule.front() == '*') {
            rules.suffixes.emplace_back(rule.substr(1));
        } else if (dir_only) {
            rules.dir_names.emplace_back(rule);
        } else {
            rules.exact_names.emplace_back(rule);
        }
    }
}

bool should_skip_file(std::string_view name)
{
    const size_t dot_pos = name.find_last_of('.');

    if (dot_pos == std::string::npos || dot_pos == 0) {
        return true;
    }

    const std::string_view ext(name.data() + dot_pos, name.size() - dot_pos);

    switch (ext.size()) {
        case 2:
            return ext == ".a" || ext == ".o";
        case 3:
            return ext == ".so" || ext == ".db" || ext == ".gz" ||
                ext == ".xz" || ext == ".7z";
        case 4:
            return ext == ".exe" || ext == ".dll" || ext == ".bin" ||
                ext == ".png" || ext == ".jpg" || ext == ".JPG" ||
                ext == ".pdf" || ext == ".pyc" || ext == ".zip" || ext == ".tar" ||
                ext == ".jar" || ext == ".mp3" || ext == ".mp4" ||
                ext == ".mov" || ext == ".avi" || ext == ".gif" ||
                ext == ".bmp" || ext == ".ico" || ext == ".ttf" ||
                ext == ".otf";
        case 5:
            return ext == ".jpeg" || ext == ".webp" || ext == ".class";
        case 6:
            return ext == ".cmake" || ext == ".dylib";
        case 7:
            return ext == ".sqlite" || ext == ".sqlite3";
        default:
            return false;
    }
}

bool should_skip_dir(std::string_view name)
{
    return (!name.empty() && name[0] == '.') ||
        name == "build" || name == "build-release" || name == "build-debug" ||
        name == "node_modules" || name == "target" || name == "dist" ||
        name == "out" || name == "__pycache__" || name == "Unity" ||
        name == "Library";
}

bool string_view_ends_with(std::string_view value, std::string_view suffix)
{
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool has_exact_ignore_match(std::string_view name, const std::vector<std::string>& rules)
{
    for (const auto& rule : rules) {
        if (name == rule) {
            return true;
        }
    }

    return false;
}

bool should_ignore_file(std::string_view name, const IgnoreRules& rules)
{
    if (has_exact_ignore_match(name, rules.exact_names)) {
        return true;
    }

    for (const auto& suffix : rules.suffixes) {
        if (string_view_ends_with(name, suffix)) {
            return true;
        }
    }

    return false;
}

bool should_ignore_dir(std::string_view name, const IgnoreRules& rules)
{
    if (has_exact_ignore_match(name, rules.dir_names) ||
        has_exact_ignore_match(name, rules.exact_names)) {
        return true;
    }

    for (const auto& suffix : rules.suffixes) {
        if (string_view_ends_with(name, suffix)) {
            return true;
        }
    }

    return false;
}
