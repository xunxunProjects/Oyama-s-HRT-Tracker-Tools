#pragma once

#include "error.h"
#include "http.h"

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

struct LoginResult {
    std::string token;
    std::string user_id;
    std::string username;
    std::string device_pass;
    int api_version = 1;
};

struct Snapshot {
    std::string id;
    int64_t created_at = 0;
    nlohmann::json data;
};

class ApiClient {
public:
    virtual ~ApiClient() = default;

    virtual LoginResult login(const std::string &username, const std::string &password,
                              const std::string &totp_code, const std::string &backup_code,
                              const std::string &device_pass) = 0;
    virtual void logout() = 0;
    virtual nlohmann::json me() = 0;
    virtual std::optional<Snapshot> latest_snapshot() = 0;
    virtual Snapshot put_snapshot(const nlohmann::json &data,
                                  const std::string &if_match) = 0;
    virtual nlohmann::json server_version() = 0;
};

std::unique_ptr<ApiClient> make_api_client(const std::string &server,
                                           const HttpOptions &http_options);
