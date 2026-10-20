#include "api.h"

#include "hrt_client.h"
#include "http.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string trim_trailing_slashes(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

nlohmann::json parse_json(const std::string &body, const char *what) {
    nlohmann::json parsed =
        nlohmann::json::parse(body.begin(), body.end(), nullptr, false);
    if (parsed.is_discarded()) {
        throw ToolError(ExitCode::Network, "HTTP_RESPONSE",
                        std::string("server returned invalid JSON for ") + what);
    }
    return parsed;
}

std::string error_code(const nlohmann::json &body) {
    if (body.is_object() && body.contains("code") && body.at("code").is_string()) {
        return body.at("code").get<std::string>();
    }
    return "HTTP_ERROR";
}

std::string error_message(const nlohmann::json &body) {
    if (body.is_object() && body.contains("message") && body.at("message").is_string()) {
        return body.at("message").get<std::string>();
    }
    return "request failed";
}

class LiveApiClient : public ApiClient {
public:
    LiveApiClient(std::string server, HttpOptions options, std::string token)
        : server_(trim_trailing_slashes(std::move(server))),
          options_(std::move(options)),
          token_(std::move(token)) {}

    void set_token(const std::string &token) override { token_ = token; }

    LoginResult login(const std::string &username, const std::string &password,
                      const std::string &totp_code, const std::string &backup_code,
                      const std::string &device_pass) override {
        nlohmann::json body{{"username", username}, {"password", password}};
        if (!totp_code.empty()) {
            body["totp_code"] = totp_code;
        }
        if (!backup_code.empty()) {
            body["backup_code"] = backup_code;
        }
        if (!device_pass.empty()) {
            body["device_pass"] = device_pass;
        }
        HttpResponse response =
            request("POST", "/login", body.dump(), false);
        if (response.status == 401) {
            const nlohmann::json parsed =
                parse_json(response.body, "login");
            if (error_code(parsed) == "TWO_FACTOR_REQUIRED") {
                const std::string method =
                    parsed.value("method", std::string("totp"));
                throw ApiTwoFactorRequired(method);
            }
            throw_http_status(response.status, response.body, "login");
        }
        if (response.status < 200 || response.status >= 300) {
            throw_http_status(response.status, response.body, "login");
        }
        LoginResult result = parse_login_response(parse_json(response.body, "login"));
        token_ = result.token;
        return result;
    }

    void logout() override {
        HttpResponse response = request("POST", "/logout", "", true);
        /* A token whose session is already gone is still logged out. */
        if (response.status == 401 || response.status == 403) {
            return;
        }
        if (response.status < 200 || response.status >= 300) {
            throw_http_status(response.status, response.body, "logout");
        }
    }

    nlohmann::json me() override {
        HttpResponse response = request("GET", "/me", "", true);
        if (response.status < 200 || response.status >= 300) {
            throw_http_status(response.status, response.body, "me");
        }
        nlohmann::json parsed = parse_json(response.body, "me");
        if (!parsed.is_object() || !parsed.contains("user") ||
            !parsed.at("user").is_object()) {
            throw ToolError(ExitCode::Data, "ME_RESPONSE",
                            "server returned an invalid identity");
        }
        return parsed;
    }

    std::optional<Snapshot> latest_snapshot() override {
        HttpResponse response = request("GET", "/snapshot", "", true);
        if (response.status == 404) {
            const nlohmann::json parsed =
                parse_json(response.body, "snapshot");
            if (error_code(parsed) == "NO_SNAPSHOT") {
                return std::nullopt;
            }
        }
        if (response.status < 200 || response.status >= 300) {
            throw_http_status(response.status, response.body, "snapshot");
        }
        return parse_snapshot_response(parse_json(response.body, "snapshot"));
    }

    Snapshot snapshot_by_id(const std::string &id) override {
        if (!valid_snapshot_id(id)) {
            throw ToolError(ExitCode::Usage, "SNAPSHOT_ID",
                            "invalid snapshot id");
        }
        HttpResponse response = request("GET", "/snapshot/" + id, "", true);
        if (response.status < 200 || response.status >= 300) {
            throw_http_status(response.status, response.body, "snapshot");
        }
        return parse_snapshot_response(parse_json(response.body, "snapshot"));
    }

    nlohmann::json list_snapshots() override {
        HttpResponse response = request("GET", "/snapshots", "", true);
        if (response.status < 200 || response.status >= 300) {
            throw_http_status(response.status, response.body, "snapshot list");
        }
        nlohmann::json parsed = parse_json(response.body, "snapshot list");
        if (!parsed.is_array()) {
            throw ToolError(ExitCode::Data, "SNAPSHOT_RESPONSE",
                            "snapshot list is not an array");
        }
        return parsed;
    }

    Snapshot put_snapshot(const nlohmann::json &data,
                          const std::string &if_match) override {
        if (!if_match.empty() && !valid_snapshot_id(if_match)) {
            throw ToolError(ExitCode::Usage, "SNAPSHOT_ID",
                            "invalid base revision id");
        }
        const std::string body = nlohmann::json{{"data", data}}.dump();
        HttpResponse response = request("PUT", "/snapshot", body, true, if_match);
        if (response.status == 409) {
            const auto [current_id, current_created_at] = parse_conflict_current(
                parse_json(response.body, "snapshot write"));
            throw SnapshotConflictError(current_id, current_created_at);
        }
        if (response.status < 200 || response.status >= 300) {
            throw_http_status(response.status, response.body, "snapshot write");
        }
        const nlohmann::json parsed = parse_json(response.body, "snapshot write");
        Snapshot snapshot;
        snapshot.id = parsed.is_object() ? parsed.value("id", std::string{})
                                         : std::string{};
        snapshot.created_at =
            parsed.is_object() ? parsed.value("createdAt", int64_t{0}) : 0;
        if (!valid_snapshot_id(snapshot.id)) {
            throw ToolError(ExitCode::Data, "SNAPSHOT_RESPONSE",
                            "snapshot write response has no valid id");
        }
        return snapshot;
    }

    nlohmann::json server_version() override {
        HttpResponse response = request("GET", "/version", "", false);
        if (response.status < 200 || response.status >= 300) {
            throw_http_status(response.status, response.body, "version");
        }
        return parse_json(response.body, "version");
    }

private:
    HttpResponse request(const std::string &method, const std::string &path,
                         const std::string &body, bool authenticated,
                         const std::string &if_match = "") {
        std::vector<std::string> headers;
        headers.push_back("Accept: application/json");
        if (!body.empty()) {
            headers.push_back("Content-Type: application/json");
        }
        if (authenticated) {
            if (token_.empty()) {
                throw ApiAuthError("NOT_LOGGED_IN",
                                   "log in first; the vault has no token");
            }
            headers.push_back("Authorization: Bearer " + token_);
        }
        if (!if_match.empty()) {
            headers.push_back("If-Match: " + if_match);
        }
        HttpClient client;
        return client.request(method, server_ + "/api/tool/v1" + path, headers, body,
                              options_);
    }

    std::string server_;
    HttpOptions options_;
    std::string token_;
};

} // namespace

void throw_http_status(long status, const std::string &body, const char *what) {
    if (status >= 300 && status < 400) {
        throw ToolError(ExitCode::Network, "REDIRECT",
                        "server redirected; refusing to follow (" +
                            std::to_string(status) + ")");
    }
    nlohmann::json parsed =
        nlohmann::json::parse(body.begin(), body.end(), nullptr, false);
    const std::string code = parsed.is_discarded() ? "HTTP_" + std::to_string(status)
                                                   : error_code(parsed);
    const std::string message = parsed.is_discarded()
                                    ? std::string(what) + " failed with HTTP " +
                                          std::to_string(status)
                                    : error_message(parsed);
    if (status == 401 || status == 403) {
        throw ApiAuthError(code, message);
    }
    throw ToolError(ExitCode::Network, code, message);
}


LoginResult parse_login_response(const nlohmann::json &body) {
    if (!body.is_object() || !body.contains("token") ||
        !body.at("token").is_string()) {
        throw ToolError(ExitCode::Data, "LOGIN_RESPONSE",
                        "login response has no token");
    }
    LoginResult result;
    result.token = body.at("token").get<std::string>();
    if (!body.contains("user") || !body.at("user").is_object()) {
        throw ToolError(ExitCode::Data, "LOGIN_RESPONSE",
                        "login response has no user");
    }
    const nlohmann::json &user = body.at("user");
    result.user_id = user.value("id", std::string{});
    result.username = user.value("username", std::string{});
    result.device_pass = body.value("devicePass", std::string{});
    result.api_version = body.value("apiVersion", 1);
    const bool safe_username =
        !result.username.empty() && result.username.size() <= 64 &&
        std::all_of(result.username.begin(), result.username.end(),
                    [](unsigned char c) { return c >= 0x20 && c != 0x7f; });
    if (!valid_bearer_token(result.token) || !valid_snapshot_id(result.user_id) ||
        !safe_username ||
        (!result.device_pass.empty() && !valid_bearer_token(result.device_pass))) {
        throw ToolError(ExitCode::Data, "LOGIN_RESPONSE",
                        "login response contains invalid fields");
    }
    return result;
}

Snapshot parse_snapshot_response(const nlohmann::json &body) {
    if (!body.is_object() || !body.contains("id") ||
        !body.at("id").is_string()) {
        throw ToolError(ExitCode::Data, "SNAPSHOT_RESPONSE",
                        "snapshot response has no id");
    }
    if (!body.contains("data")) {
        throw ToolError(ExitCode::Data, "SNAPSHOT_RESPONSE",
                        "snapshot response has no data");
    }
    Snapshot snapshot;
    snapshot.id = body.at("id").get<std::string>();
    if (!valid_snapshot_id(snapshot.id)) {
        throw ToolError(ExitCode::Data, "SNAPSHOT_RESPONSE",
                        "snapshot response has an invalid id");
    }
    snapshot.created_at = body.value("createdAt", int64_t{0});
    snapshot.data = body.at("data");
    return snapshot;
}

bool valid_bearer_token(const std::string &token) {
    if (token.empty() || token.size() > 4096) {
        return false;
    }
    return std::all_of(token.begin(), token.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' ||
               c == '=';
    });
}

bool valid_snapshot_id(const std::string &id) {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

std::pair<std::string, int64_t> parse_conflict_current(const nlohmann::json &body) {
    std::string id;
    int64_t created_at = 0;
    if (body.is_object() && body.contains("current") &&
        body.at("current").is_object()) {
        const nlohmann::json &current = body.at("current");
        if (current.contains("id") && current.at("id").is_string()) {
            id = current.at("id").get<std::string>();
        }
        if (current.contains("createdAt") && current.at("createdAt").is_number()) {
            created_at = current.at("createdAt").get<int64_t>();
        }
    }
    if (!id.empty() && !valid_snapshot_id(id)) {
        id.clear();
    }
    return {id, created_at};
}

std::unique_ptr<ApiClient> make_api_client(const std::string &server,
                                           const HttpOptions &http_options,
                                           const std::string &token) {
    if (is_hrt_url(server)) {
        return make_hrt_api_client(server, http_options, token);
    }
    return std::make_unique<LiveApiClient>(server, http_options, token);
}
