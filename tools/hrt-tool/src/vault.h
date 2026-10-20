#pragma once

#include "error.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

struct VaultData {
    int version = 1;
    std::string token;
    std::string user_id;
    std::string username;
    std::string device_pass;
    std::string cloud_key;
    std::string server = "https://hrt.mahiro.uk";
    std::string proxy;
    nlohmann::json trust = nlohmann::json::object();
    bool opsec_ack = false;
    /* Milliseconds since the epoch; 0 means no recent failed login. */
    int64_t last_login_failed_at = 0;
    int64_t created_at = 0;
    int64_t updated_at = 0;
};

class VaultSession {
public:
    VaultSession() = default;
    VaultSession(VaultData data, std::array<uint8_t, 32> key);
    VaultSession(VaultSession &&other) noexcept;
    VaultSession &operator=(VaultSession &&other) noexcept;
    VaultSession(const VaultSession &) = delete;
    VaultSession &operator=(const VaultSession &) = delete;
    ~VaultSession();

    VaultData data;
    std::array<uint8_t, 32> key{};
    std::array<uint8_t, 32> log_key{};

private:
    void wipe();
};

class Vault {
public:
    static bool exists(const std::filesystem::path &path);

    static VaultSession create(const std::filesystem::path &path, const std::string &password,
                               bool overwrite = false);

    static VaultSession unlock(const std::filesystem::path &path, const std::string &password);

    static void save(const std::filesystem::path &path, const VaultData &data,
                     const std::string &password);
};
