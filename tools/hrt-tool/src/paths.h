#pragma once

#include <filesystem>
#include <string>

struct Paths {
    std::filesystem::path config_dir;
    std::filesystem::path vault_file;
};

Paths resolve_paths(const std::string &override_dir);
