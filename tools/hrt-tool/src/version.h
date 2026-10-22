#pragma once

#include <string>

struct ToolVersion {
    int major;
    int minor;
    int patch;
};

ToolVersion current_version();
std::string current_version_string();
std::string format_user_agent();

std::string format_version_line();
std::string format_version_json();
