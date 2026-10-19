#pragma once

#include "error.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

class SnapshotAuthError : public ToolError {
public:
    explicit SnapshotAuthError(const std::string &message = "snapshot authentication failed")
        : ToolError(ExitCode::Vault, "SNAPSHOT_AUTH", message) {}
};

class SnapshotCorruptError : public ToolError {
public:
    explicit SnapshotCorruptError(const std::string &message)
        : ToolError(ExitCode::Data, "SNAPSHOT_CORRUPT", message) {}
};

std::array<uint8_t, 32> derive_cloud_key(const std::string &password,
                                         const std::string &user_id);

bool is_cloud_bundle(const nlohmann::json &value);

nlohmann::json decrypt_cloud_bundle(const nlohmann::json &bundle,
                                    const std::array<uint8_t, 32> &key);

nlohmann::json encrypt_cloud_bundle(const nlohmann::json &payload,
                                    const std::array<uint8_t, 32> &key);

nlohmann::json load_snapshot(const std::filesystem::path &path,
                             const std::string &account_password,
                             const std::string &user_id);

void save_snapshot(const std::filesystem::path &path, const nlohmann::json &payload,
                   const std::string &account_password, const std::string &user_id);
