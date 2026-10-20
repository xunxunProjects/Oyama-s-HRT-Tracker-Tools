#include "api.h"
#include "error.h"
#include "hrt_client.h"
#include "hrt_tls_stub.h"
#include "test_framework.h"

#include <csignal>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct Seen {
    std::string op;
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
};

bool has_api_error(const ToolError &error, const std::string &code) {
    return error.code() == code;
}

std::string header_of(const Seen &item, const std::string &name) {
    const auto found = item.headers.find(name);
    return found == item.headers.end() ? std::string{} : found->second;
}

} // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN);

    {
        std::mutex mutex;
        std::vector<Seen> seen;
        HrtTlsStubServer server(
            [&](const nlohmann::json &frame) {
                Seen item;
                item.op = frame.value("op", std::string{});
                item.method = frame.value("method", std::string{});
                item.path = frame.value("path", std::string{});
                if (frame.contains("headers") && frame.at("headers").is_object()) {
                    for (const auto &entry : frame.at("headers").items()) {
                        item.headers[entry.key()] = entry.value().get<std::string>();
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    seen.push_back(item);
                }
                if (item.op != "request") {
                    return HrtStubResponse{200, {{"ok", true}}};
                }
                if (item.path == "/api/tool/v1/me") {
                    return HrtStubResponse{
                        200, {{"user", {{"id", "u-1"}, {"username", "alice"}}}}};
                }
                if (item.path == "/api/tool/v1/snapshot" && item.method == "GET") {
                    return HrtStubResponse{
                        404, {{"code", "NO_SNAPSHOT"}, {"message", "no snapshot"}}};
                }
                if (item.path == "/api/tool/v1/snapshot" && item.method == "PUT") {
                    return HrtStubResponse{409,
                                           {{"code", "CONFLICT"},
                                            {"current",
                                             {{"id", "rev-2"}, {"createdAt", 123}}}}};
                }
                if (item.path == "/api/tool/v1/logout") {
                    return HrtStubResponse{200, {{"ok", true}}};
                }
                if (item.path == "/api/tool/v1/version") {
                    return HrtStubResponse{200, {{"version", "9.9.9"}}};
                }
                return HrtStubResponse{404, {{"code", "NOT_FOUND"}}};
            },
            "api-stub");
        CHECK(server.ok());
        if (server.ok()) {
            HrtClientOptions probe_options;
            probe_options.timeout_s = 5;
            HrtClient probe(server.url(), probe_options);
            const HrtServerCertificate certificate = probe.connect();
            probe.close();

            const HrtDeviceIdentity identity =
                generate_hrt_device_identity("api-test-device");
            HttpOptions options;
            options.timeout_s = 5;
            options.pinned_spki_sha256 = certificate.spki_sha256;
            options.hrt_device_cert_pem = identity.certificate_pem;
            options.hrt_device_key_pem = identity.private_key_pem;

            std::unique_ptr<ApiClient> client =
                make_api_client(server.url(), options, "tok.abc");
            CHECK(client != nullptr);

            const nlohmann::json me = client->me();
            CHECK_EQ(me.at("user").value("id", std::string{}), std::string("u-1"));

            CHECK(!client->latest_snapshot().has_value());

            try {
                client->put_snapshot(nlohmann::json::object(), "rev-1");
                CHECK(false);
            } catch (const SnapshotConflictError &conflict) {
                CHECK_EQ(conflict.current_id(), std::string("rev-2"));
                CHECK_EQ(conflict.current_created_at(), int64_t{123});
            }

            client->logout();

            CHECK(!server.saw_client_certificate());
            for (const nlohmann::json &frame : server.frames()) {
                if (frame.value("op", std::string{}) == "request") {
                    CHECK(frame.contains("device_certificate"));
                    CHECK(frame.contains("device_signature"));
                }
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                bool saw_me = false;
                bool saw_put = false;
                bool saw_logout = false;
                for (const Seen &item : seen) {
                    if (item.op != "request") {
                        continue;
                    }
                    CHECK_EQ(header_of(item, "authorization"),
                             std::string("Bearer tok.abc"));
                    if (item.path == "/api/tool/v1/me") {
                        saw_me = true;
                        CHECK_EQ(item.method, std::string("GET"));
                    }
                    if (item.path == "/api/tool/v1/snapshot" &&
                        item.method == "PUT") {
                        saw_put = true;
                        CHECK_EQ(header_of(item, "if-match"), std::string("rev-1"));
                    }
                    if (item.path == "/api/tool/v1/logout") {
                        saw_logout = true;
                        CHECK_EQ(item.method, std::string("POST"));
                    }
                }
                CHECK(saw_me);
                CHECK(saw_put);
                CHECK(saw_logout);
            }

            /* login over raw transport is pairing, never a password form. */
            try {
                client->login("alice", "hunter2", "", "", "");
                CHECK(false);
            } catch (const ToolError &error) {
                CHECK(has_api_error(error, "PAIRING_REQUIRED"));
            }
        }
    }

    {
        HrtTlsStubServer server(
            [](const nlohmann::json &) {
                return HrtStubResponse{200, {{"version", "9.9.9"}}};
            },
            "api-no-token-stub");
        CHECK(server.ok());
        if (server.ok()) {
            HrtClientOptions probe_options;
            probe_options.timeout_s = 5;
            HrtClient probe(server.url(), probe_options);
            const HrtServerCertificate certificate = probe.connect();
            probe.close();

            const HrtDeviceIdentity identity =
                generate_hrt_device_identity("api-no-token-device");
            HttpOptions options;
            options.timeout_s = 5;
            options.pinned_spki_sha256 = certificate.spki_sha256;
            options.hrt_device_cert_pem = identity.certificate_pem;
            options.hrt_device_key_pem = identity.private_key_pem;

            std::unique_ptr<ApiClient> client = make_api_client(server.url(), options);
            const nlohmann::json version = client->server_version();
            CHECK_EQ(version.value("version", std::string{}), std::string("9.9.9"));
            try {
                client->me();
                CHECK(false);
            } catch (const ApiAuthError &error) {
                CHECK(has_api_error(error, "NOT_LOGGED_IN"));
            }
        }
    }

    {
        HrtTlsStubServer server(
            [](const nlohmann::json &) {
                return HrtStubResponse{200, {{"user", {{"id", "u-1"}}}}};
            },
            "api-missing-identity-stub");
        CHECK(server.ok());
        if (server.ok()) {
            HrtClientOptions probe_options;
            probe_options.timeout_s = 5;
            HrtClient probe(server.url(), probe_options);
            const HrtServerCertificate certificate = probe.connect();
            probe.close();

            HttpOptions options;
            options.timeout_s = 5;
            options.pinned_spki_sha256 = certificate.spki_sha256;
            std::unique_ptr<ApiClient> client = make_api_client(server.url(), options, "tok.abc");
            try {
                client->me();
                CHECK(false);
            } catch (const ToolError &error) {
                CHECK(has_api_error(error, "HRT_IDENTITY_MISSING"));
            }
            CHECK(server.frames().empty());
        }
    }

    {
        HrtTlsStubServer server(
            [](const nlohmann::json &) {
                return HrtStubResponse{200, {{"user", {{"id", "u-1"}}}}};
            },
            "api-unpinned-stub");
        CHECK(server.ok());
        if (server.ok()) {
            const HrtDeviceIdentity identity =
                generate_hrt_device_identity("api-unpinned-device");
            HttpOptions options;
            options.timeout_s = 5;
            options.hrt_device_cert_pem = identity.certificate_pem;
            options.hrt_device_key_pem = identity.private_key_pem;
            std::unique_ptr<ApiClient> client = make_api_client(server.url(), options, "tok.abc");
            try {
                client->me();
                CHECK(false);
            } catch (const ToolError &error) {
                CHECK(has_api_error(error, "HRT_TRUST"));
            }
        }
    }

    {
        HrtTlsStubServer server(
            [](const nlohmann::json &) {
                return HrtStubResponse{0,
                                       nlohmann::json::object(),
                                       {{"code", "CERT_REQUIRED"},
                                        {"message", "a paired client certificate is required"}}};
            },
            "api-raw-error-stub");
        CHECK(server.ok());
        if (server.ok()) {
            HrtClientOptions probe_options;
            probe_options.timeout_s = 5;
            HrtClient probe(server.url(), probe_options);
            const HrtServerCertificate certificate = probe.connect();
            probe.close();

            const HrtDeviceIdentity identity =
                generate_hrt_device_identity("api-raw-error-device");
            HttpOptions options;
            options.timeout_s = 5;
            options.pinned_spki_sha256 = certificate.spki_sha256;
            options.hrt_device_cert_pem = identity.certificate_pem;
            options.hrt_device_key_pem = identity.private_key_pem;
            std::unique_ptr<ApiClient> client = make_api_client(server.url(), options, "tok.abc");
            try {
                client->me();
                CHECK(false);
            } catch (const ToolError &error) {
                CHECK(has_api_error(error, "CERT_REQUIRED"));
            }
        }
    }

    return test_result();
}
