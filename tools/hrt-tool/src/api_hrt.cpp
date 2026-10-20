#include "api.h"

#include "hrt_client.h"
#include "hrt_protocol.h"

#include <map>
#include <memory>
#include <string>
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

bool is_device_auth_error(const std::string &code) {
    return code == "CERT_REQUIRED" || code == "CERT_INVALID" ||
           code == "NOT_AUTHORIZED";
}

/* The authenticated half of the raw protocol: every call rides one pinned TLS
   1.3 connection and presents the vault's device certificate. */
class HrtApiClient : public ApiClient {
public:
    HrtApiClient(std::string server, HttpOptions options, std::string token)
        : server_(trim_trailing_slashes(std::move(server))),
          options_(std::move(options)),
          token_(std::move(token)) {}

    void set_token(const std::string &token) override { token_ = token; }

    LoginResult login(const std::string &, const std::string &, const std::string &,
                      const std::string &, const std::string &) override {
        throw ToolError(ExitCode::Usage, "PAIRING_REQUIRED",
                        "hrt:// accounts sign in by web approval, not a password "
                        "form; run hrt-tool login");
    }

    void logout() override {
        HttpResponse response = request("POST", "/logout", "", true);
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
            const nlohmann::json parsed = parse_json(response.body, "snapshot");
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
            throw ToolError(ExitCode::Usage, "SNAPSHOT_ID", "invalid snapshot id");
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
    void ensure_connected() {
        if (client_ != nullptr) {
            return;
        }
        if (options_.pinned_spki_sha256.empty()) {
            throw ToolError(ExitCode::Usage, "HRT_TRUST",
                            "the raw server has no pinned public key; run "
                            "hrt-tool login");
        }
        if (options_.hrt_device_cert_pem.empty() ||
            options_.hrt_device_key_pem.empty()) {
            throw ToolError(ExitCode::Vault, "HRT_IDENTITY_MISSING",
                            "the vault has no device identity for this server; "
                            "run hrt-tool login");
        }
        HrtClientOptions options;
        options.proxy = options_.proxy;
        options.client_cert_pem = options_.hrt_device_cert_pem;
        options.client_key_pem = options_.hrt_device_key_pem;
        options.expected_spki = options_.pinned_spki_sha256;
        options.timeout_s = options_.timeout_s;
        client_ = std::make_unique<HrtClient>(server_, options);
        client_->connect();
    }

    HttpResponse request(const std::string &method, const std::string &path,
                         const std::string &body, bool authenticated,
                         const std::string &if_match = "") {
        if (authenticated && token_.empty()) {
            throw ApiAuthError("NOT_LOGGED_IN",
                               "log in first; the vault has no token");
        }
        ensure_connected();
        std::map<std::string, std::string> headers;
        headers["accept"] = "application/json";
        if (!body.empty()) {
            headers["content-type"] = "application/json";
        }
        if (authenticated) {
            headers["authorization"] = "Bearer " + token_;
        }
        if (!if_match.empty()) {
            headers["if-match"] = if_match;
        }
        nlohmann::json frame;
        try {
            frame = client_->request(method, "/api/tool/v1" + path, headers, body);
        } catch (const ToolError &error) {
            if (is_device_auth_error(error.code())) {
                throw ApiAuthError(error.code(), error.what());
            }
            throw;
        }
        HttpResponse response;
        response.status = frame.value("status", 0L);
        response.body = base64_decode(frame.value("body_base64", std::string{}));
        if (frame.contains("headers") && frame.at("headers").is_object()) {
            for (const auto &entry : frame.at("headers").items()) {
                if (entry.value().is_string()) {
                    response.headers.emplace(entry.key(),
                                             entry.value().get<std::string>());
                }
            }
        }
        return response;
    }

    std::string server_;
    HttpOptions options_;
    std::string token_;
    std::unique_ptr<HrtClient> client_;
};

} // namespace

std::unique_ptr<ApiClient> make_hrt_api_client(const std::string &server,
                                               const HttpOptions &http_options,
                                               const std::string &token) {
    return std::make_unique<HrtApiClient>(server, http_options, token);
}
