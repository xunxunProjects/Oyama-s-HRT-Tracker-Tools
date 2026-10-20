#include "hrt_pairing.h"

#include "api.h"
#include "error.h"
#include "hrt_protocol.h"
#include "http.h"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <string>

namespace {

bool is_lower_hex(const std::string &value, size_t length) {
    return value.size() == length &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

bool has_control_character(const std::string &value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char c) {
        return c < 0x20 || c == 0x7f;
    });
}

std::string trim_ascii(const std::string &value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

std::string checked_label(const std::string &value, const char *field) {
    const std::string label = trim_ascii(value);
    if (label.empty() || label.size() > 64 || has_control_character(label)) {
        throw ToolError(ExitCode::Usage, "PAIR_INPUT",
                        std::string("invalid ") + field);
    }
    return label;
}

nlohmann::json decode_body(const nlohmann::json &response, const char *what) {
    const std::string body = base64_decode(response.value("body_base64", std::string{}));
    nlohmann::json parsed = nlohmann::json::parse(body.begin(), body.end(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        throw ToolError(ExitCode::Data, "PAIR_RESPONSE",
                        std::string(what) + " returned an invalid body");
    }
    return parsed;
}

void throw_frame_error(const nlohmann::json &response) {
    if (!response.contains("error") || !response.at("error").is_object()) {
        return;
    }
    const nlohmann::json &error = response.at("error");
    throw ToolError(ExitCode::Network, error.value("code", std::string("HRT_ERROR")),
                    error.value("message", std::string("hrt pairing failed")));
}

void throw_status_error(long status, const nlohmann::json &body, const char *what) {
    if (status < 200 || status >= 300) {
        throw ToolError(ExitCode::Network, body.value("code", std::string("PAIR_FAILED")),
                        body.value("message", std::string(what) + " failed"));
    }
}

std::string checked_verification_url(const std::string &value) {
    if (value.empty() || value.size() > 2048) {
        throw ToolError(ExitCode::Data, "PAIR_RESPONSE",
                        "pairing response has no valid verification URL");
    }
    const Url url = parse_url(value);
    if (url.scheme != "https" && url.scheme != "http") {
        throw ToolError(ExitCode::Data, "PAIR_RESPONSE",
                        "pairing verification URL is not HTTP(S)");
    }
    validate_server_url(url);
    return value;
}

std::string optional_device_label(const nlohmann::json &value) {
    if (!value.is_string()) {
        return {};
    }
    const std::string label = value.get<std::string>();
    if (label.size() > 64 || has_control_character(label)) {
        throw ToolError(ExitCode::Data, "PAIR_RESPONSE",
                        "pairing response has an invalid device name");
    }
    return label;
}

} // namespace

HrtPairRequest start_hrt_pairing(HrtClient &client, const std::string &username,
                                 const std::string &device_name,
                                 const std::string &device_certificate_pem,
                                 const std::string &code,
                                 const std::string &client_version) {
    const std::string account = checked_label(username, "username");
    const std::string device = checked_label(device_name, "device name");
    if (!is_lower_hex(code, 128)) {
        throw ToolError(ExitCode::Usage, "PAIR_INPUT",
                        "the verification code must be 128 lowercase hex characters");
    }
    if (device_certificate_pem.empty() || device_certificate_pem.size() > 32 * 1024 ||
        device_certificate_pem.find("-----BEGIN CERTIFICATE-----") == std::string::npos) {
        throw ToolError(ExitCode::Usage, "PAIR_INPUT",
                        "the device certificate is missing or invalid");
    }
    if (client_version.size() > 64 || has_control_character(client_version)) {
        throw ToolError(ExitCode::Usage, "PAIR_INPUT", "invalid client version");
    }

    nlohmann::json body{{"username", account},
                        {"device_name", device},
                        {"device_certificate", device_certificate_pem},
                        {"code_hash", sha256_hex(code)}};
    if (!client_version.empty()) {
        body["client_version"] = client_version;
    }
    const nlohmann::json response =
        client.call({{"op", "pair.start"},
                     {"body_base64", base64_encode(body.dump())}});
    throw_frame_error(response);
    const nlohmann::json parsed = decode_body(response, "pair.start");
    throw_status_error(response.value("status", 0), parsed, "pairing start");

    HrtPairRequest request;
    request.request_id = parsed.value("request_id", std::string{});
    request.poll_secret = parsed.value("poll_secret", std::string{});
    request.verification_url =
        checked_verification_url(parsed.value("verification_url", std::string{}));
    if (parsed.contains("expires_at") && parsed.at("expires_at").is_number_integer()) {
        request.expires_at = parsed.at("expires_at").get<int64_t>();
    }
    if (!is_lower_hex(request.request_id, 32) ||
        !is_lower_hex(request.poll_secret, 64) || request.expires_at <= 0) {
        throw ToolError(ExitCode::Data, "PAIR_RESPONSE",
                        "pairing response is missing its request id, secret, or expiry");
    }
    return request;
}

HrtPairResult poll_hrt_pairing(HrtClient &client, const HrtPairRequest &request) {
    if (!is_lower_hex(request.request_id, 32) ||
        !is_lower_hex(request.poll_secret, 64)) {
        throw ToolError(ExitCode::Usage, "PAIR_INPUT",
                        "invalid pairing request id or secret");
    }
    const nlohmann::json body{{"request_id", request.request_id},
                              {"poll_secret", request.poll_secret}};
    const nlohmann::json response =
        client.call({{"op", "pair.poll"},
                     {"body_base64", base64_encode(body.dump())}});
    throw_frame_error(response);
    const nlohmann::json parsed = decode_body(response, "pair.poll");
    throw_status_error(response.value("status", 0), parsed, "pairing poll");

    const std::string status = parsed.value("status", std::string{});
    HrtPairResult result;
    if (status == "pending") {
        result.status = HrtPairStatus::Pending;
        return result;
    }
    if (status == "denied") {
        result.status = HrtPairStatus::Denied;
        return result;
    }
    if (status == "expired") {
        result.status = HrtPairStatus::Expired;
        return result;
    }
    if (status != "approved") {
        throw ToolError(ExitCode::Data, "PAIR_RESPONSE",
                        "pairing response has an unknown status");
    }

    LoginResult login =
        parse_login_response({{"token", parsed.value("token", std::string{})},
                              {"user", parsed.contains("user")
                                           ? parsed.at("user")
                                           : nlohmann::json::object()}});
    result.status = HrtPairStatus::Approved;
    result.token = std::move(login.token);
    result.user_id = std::move(login.user_id);
    result.username = std::move(login.username);
    if (parsed.contains("device") && parsed.at("device").is_object()) {
        const nlohmann::json &device = parsed.at("device");
        const std::string fingerprint =
            device.value("fingerprint", std::string{});
        if (!fingerprint.empty() && !is_lower_hex(fingerprint, 64)) {
            throw ToolError(ExitCode::Data, "PAIR_RESPONSE",
                            "pairing response has an invalid device fingerprint");
        }
        result.device_fingerprint = fingerprint;
        result.device_name = optional_device_label(
            device.contains("name") ? device.at("name") : nlohmann::json{});
    }
    return result;
}
