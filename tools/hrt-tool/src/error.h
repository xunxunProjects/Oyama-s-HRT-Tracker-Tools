#pragma once

#include <stdexcept>
#include <string>
#include <utility>

enum class ExitCode : int {
    Ok = 0,
    Internal = 1,
    Usage = 2,
    Vault = 3,
    Network = 4,
    Data = 5,
    NotImplemented = 6,
    Auth = 7,
    Trust = 8,
    Permission = 9,
    Conflict = 10,
    Cancelled = 11,
    Config = 12,
    F2F = 13,
    Platform = 14,
};

const char *exit_code_name(ExitCode code);
const char *exit_code_description(ExitCode code);

class ToolError : public std::runtime_error {
public:
    ToolError(ExitCode exit_code, std::string code, const std::string &message)
        : std::runtime_error(message), exit_code_(exit_code), code_(std::move(code)) {}

    ExitCode exit_code() const noexcept { return exit_code_; }
    const std::string &code() const noexcept { return code_; }

private:
    ExitCode exit_code_;
    std::string code_;
};

class VaultAuthError : public ToolError {
public:
    explicit VaultAuthError(const std::string &message = "vault authentication failed")
        : ToolError(ExitCode::Vault, "VAULT_AUTH", message) {}
};

class VaultWeakPasswordError : public ToolError {
public:
    VaultWeakPasswordError(const std::string &message, double guess_bits, double uniform_bits)
        : ToolError(ExitCode::Vault, "VAULT_WEAK_PASSWORD", message),
          guess_bits_(guess_bits),
          uniform_bits_(uniform_bits) {}

    double guess_bits() const noexcept { return guess_bits_; }
    double uniform_bits() const noexcept { return uniform_bits_; }

private:
    double guess_bits_;
    double uniform_bits_;
};

class VaultCorruptError : public ToolError {
public:
    explicit VaultCorruptError(const std::string &message)
        : ToolError(ExitCode::Data, "VAULT_CORRUPT", message) {}
};

class ToolLimitError : public ToolError {
public:
    explicit ToolLimitError(const std::string &message)
        : ToolError(ExitCode::Data, "LIMIT", message) {}
};
