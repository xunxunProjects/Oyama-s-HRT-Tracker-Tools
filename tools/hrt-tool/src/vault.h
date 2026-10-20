#pragma once

#include "error.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

struct LinkData {
    std::string url;
    std::string proxy;
    std::string mode;
    std::string token;
    std::string user_id;
    std::string username;
    std::string device_pass;
    std::string cloud_key;
    std::string device_key_pem;
    std::string device_cert_pem;
    std::string device_fingerprint;
    int64_t last_login_failed_at = 0;
};

struct VaultData {
    int version = 2;
    std::string language = "en";
    std::string welcome;
    bool humor = true;
    std::string active_link = "";
    std::map<std::string, LinkData> links;
    /* Active-link view, kept as top-level fields so existing command code
       does not need a link lookup for every read. */
    std::string mode;
    std::string token;
    std::string user_id;
    std::string username;
    std::string device_pass;
    std::string cloud_key;
    std::string server = "https://hrt.mahiro.uk";
    std::string proxy;
    nlohmann::json trust = nlohmann::json::object();
    /* hrt:// device identity. The private key is only ever written to the
       encrypted vault; the certificate and fingerprint are public. */
    std::string device_key_pem;
    std::string device_cert_pem;
    std::string device_fingerprint;
    bool opsec_ack = false;
    /* Milliseconds since the epoch; 0 means no recent failed login. */
    int64_t last_login_failed_at = 0;
    int64_t created_at = 0;
    int64_t updated_at = 0;
};

/* Exposed for migration tests: serialise/parse the plaintext payload. */
nlohmann::json vault_data_to_json(const VaultData &data);
VaultData vault_data_from_json(const nlohmann::json &value);

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
