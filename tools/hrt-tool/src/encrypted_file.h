#pragma once

#include "error.h"

#include <filesystem>
#include <string>

class ExportAuthError : public ToolError {
public:
    explicit ExportAuthError(const std::string &message = "export authentication failed")
        : ToolError(ExitCode::Vault, "EXPORT_AUTH", message) {}
};

class ExportCorruptError : public ToolError {
public:
    explicit ExportCorruptError(const std::string &message)
        : ToolError(ExitCode::Data, "EXPORT_CORRUPT", message) {}
};

void seal_encrypted_file(const std::filesystem::path &path, const std::string &magic,
                         const std::string &plaintext, const std::string &password);

std::string open_encrypted_file(const std::filesystem::path &path,
                                const std::string &magic, const std::string &password);
