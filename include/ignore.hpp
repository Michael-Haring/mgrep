#pragma once

#include <string>
#include <string_view>
#include <vector>

struct IgnoreRules {
    std::vector<std::string> exact_names;
    std::vector<std::string> dir_names;
    std::vector<std::string> suffixes;
};

void load_home_ignore(IgnoreRules& rules);
bool should_skip_file(std::string_view name);
bool should_skip_dir(std::string_view name);
bool should_ignore_file(std::string_view name, const IgnoreRules& rules);
bool should_ignore_dir(std::string_view name, const IgnoreRules& rules);
