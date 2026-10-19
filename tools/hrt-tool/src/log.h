#pragma once

#include "error.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <ostream>
#include <string>

class LogAuthError : public ToolError {
public:
    explicit LogAuthError(const std::string &message = "log authentication failed")
        : ToolError(ExitCode::Vault, "LOG_AUTH", message) {}
};

class LogCorruptError : public ToolError {
public:
    explicit LogCorruptError(const std::string &message)
        : ToolError(ExitCode::Data, "LOG_CORRUPT", message) {}
};

class EncryptedLog {
public:
    static void append(const std::filesystem::path &path,
                       const std::array<uint8_t, 32> &log_key, const std::string &line);

    static void show(const std::filesystem::path &path,
                     const std::array<uint8_t, 32> &log_key, std::ostream &out);
};
