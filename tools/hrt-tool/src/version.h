#pragma once

#include <string>

struct ToolVersion {
    int major;
    int minor;
    int patch;
};

ToolVersion current_version();

std::string format_version_line();
