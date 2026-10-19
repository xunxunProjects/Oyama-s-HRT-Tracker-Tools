#pragma once

#include "error.h"
#include "http.h"

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>

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

class ApiAuthError : public ToolError {
public:
    ApiAuthError(const std::string &code, const std::string &message)
        : ToolError(ExitCode::Vault, code, message) {}
};

class ApiTwoFactorRequired : public ToolError {
public:
    explicit ApiTwoFactorRequired(const std::string &method)
        : ToolError(ExitCode::Vault, "TWO_FACTOR_REQUIRED",
                    "a second factor is required (" + method + ")") {}
};

class SnapshotConflictError : public ToolError {
public:
    SnapshotConflictError(std::string current_id, int64_t current_created_at)
        : ToolError(ExitCode::Data, "CONFLICT", "snapshot changed; merge and retry"),
          current_id_(std::move(current_id)),
          current_created_at_(current_created_at) {}

    const std::string &current_id() const noexcept { return current_id_; }
    int64_t current_created_at() const noexcept { return current_created_at_; }

private:
    std::string current_id_;
    int64_t current_created_at_;
};

class ApiClient {
public:
    virtual ~ApiClient() = default;

    virtual void set_token(const std::string &token) = 0;
    virtual LoginResult login(const std::string &username, const std::string &password,
                              const std::string &totp_code, const std::string &backup_code,
                              const std::string &device_pass) = 0;
    virtual void logout() = 0;
    virtual nlohmann::json me() = 0;
    virtual std::optional<Snapshot> latest_snapshot() = 0;
    virtual Snapshot snapshot_by_id(const std::string &id) = 0;
    virtual nlohmann::json list_snapshots() = 0;
    virtual Snapshot put_snapshot(const nlohmann::json &data,
                                  const std::string &if_match) = 0;
    virtual nlohmann::json server_version() = 0;
};

std::unique_ptr<ApiClient> make_api_client(const std::string &server,
                                           const HttpOptions &http_options,
                                           const std::string &token = "");

/* Pure response parsers, exposed for tests. */
LoginResult parse_login_response(const nlohmann::json &body);
Snapshot parse_snapshot_response(const nlohmann::json &body);
