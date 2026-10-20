#include "error.h"
#include "hrt_client.h"
#include "hrt_pairing.h"
#include "test_framework.h"
#include "hrt_tls_stub.h"

#include <csignal>
#include <mutex>
#include <string>

namespace {

std::string hex_of(char c, size_t count) {
    return std::string(count, c);
}

HrtPairRequest sample_request() {
    HrtPairRequest request;
    request.request_id = hex_of('a', 32);
    request.poll_secret = hex_of('b', 64);
    request.expires_at = 4600000000LL;
    request.verification_url = "https://example.test/settings?hrtPair=" + request.request_id;
    return request;
}

} // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN);

    {
        std::mutex mutex;
        int requests_seen = 0;
        std::string start_body;
        HrtTlsStubServer server(
            [&](const nlohmann::json &frame) {
                std::lock_guard<std::mutex> lock(mutex);
                ++requests_seen;
                const std::string op = frame.value("op", std::string{});
                const std::string body_raw =
                    base64_decode(frame.value("body_base64", std::string{}));
                const nlohmann::json body = nlohmann::json::parse(body_raw);
                if (op == "pair.start") {
                    start_body = body_raw;
                    return HrtStubResponse{
                        200,
                        {{"request_id", hex_of('c', 32)},
                         {"poll_secret", hex_of('d', 64)},
                         {"expires_at", 4600000000LL},
                         {"verification_url",
                          "https://example.test/settings?hrtPair=" + hex_of('c', 32)}}};
                }
                return HrtStubResponse{200, {{"status", "pending"}}};
            },
            "pairing-start-stub");
        CHECK(server.ok());
        if (server.ok()) {
            HrtClientOptions probe_options;
            probe_options.timeout_s = 5;
            HrtClient probe(server.url(), probe_options);
            probe_options.expected_spki = probe.connect().spki_sha256;
            probe.close();

            const HrtDeviceIdentity identity =
                generate_hrt_device_identity("pairing-test-device");
            const std::string code = generate_hrt_code();
            HrtClient client(server.url(), probe_options);
            client.connect();
            const HrtPairRequest request =
                start_hrt_pairing(client, "Alice", "test-device",
                                  identity.certificate_pem, code, "9.9.9-test");
            client.close();
            CHECK_EQ(request.request_id, hex_of('c', 32));
            CHECK_EQ(request.poll_secret, hex_of('d', 64));
            CHECK_EQ(request.expires_at, 4600000000LL);
            CHECK_EQ(request.verification_url,
                     std::string("https://example.test/settings?hrtPair=") +
                         hex_of('c', 32));

            std::lock_guard<std::mutex> lock(mutex);
            CHECK_EQ(requests_seen, 1);
            const nlohmann::json body = nlohmann::json::parse(start_body);
            CHECK_EQ(body.value("username", std::string{}), std::string("Alice"));
            CHECK_EQ(body.value("device_name", std::string{}),
                     std::string("test-device"));
            CHECK_EQ(body.value("device_certificate", std::string{}),
                     identity.certificate_pem);
            CHECK_EQ(body.value("code_hash", std::string{}), sha256_hex(code));
            CHECK_EQ(body.value("client_version", std::string{}),
                     std::string("9.9.9-test"));
            CHECK(body.find("code") == body.end());
            CHECK_EQ(body.value("code", std::string{}), std::string{});
            const std::string dump = body.dump();
            CHECK(dump.find(code) == std::string::npos);
        }
    }

    {
        std::mutex mutex;
        int polls = 0;
        HrtTlsStubServer server(
            [&](const nlohmann::json &frame) {
                std::lock_guard<std::mutex> lock(mutex);
                ++polls;
                (void)frame;
                return HrtStubResponse{200, {{"status", "pending"}}};
            },
            "pairing-pending-stub");
        CHECK(server.ok());
        if (server.ok()) {
            HrtClientOptions options;
            options.timeout_s = 5;
            HrtClient probe(server.url(), options);
            options.expected_spki = probe.connect().spki_sha256;
            probe.close();
            HrtClient client(server.url(), options);
            client.connect();
            const HrtPairResult result = poll_hrt_pairing(client, sample_request());
            client.close();
            CHECK(result.status == HrtPairStatus::Pending);
            CHECK(result.token.empty());
            CHECK_EQ(polls, 1);
        }
    }

    {
        HrtTlsStubServer server(
            [](const nlohmann::json &) {
                return HrtStubResponse{200,
                                       {{"status", "approved"},
                                        {"token", "aaa.bbb.ccc"},
                                        {"user",
                                         {{"id", "u-123"}, {"username", "alice"}}},
                                        {"device",
                                         {{"fingerprint", hex_of('e', 64)},
                                          {"name", "test-device"}}}}};
            },
            "pairing-approved-stub");
        CHECK(server.ok());
        if (server.ok()) {
            HrtClientOptions options;
            options.timeout_s = 5;
            HrtClient probe(server.url(), options);
            options.expected_spki = probe.connect().spki_sha256;
            probe.close();
            HrtClient client(server.url(), options);
            client.connect();
            const HrtPairResult result = poll_hrt_pairing(client, sample_request());
            client.close();
            CHECK(result.status == HrtPairStatus::Approved);
            CHECK_EQ(result.token, std::string("aaa.bbb.ccc"));
            CHECK_EQ(result.user_id, std::string("u-123"));
            CHECK_EQ(result.username, std::string("alice"));
            CHECK_EQ(result.device_fingerprint, hex_of('e', 64));
            CHECK_EQ(result.device_name, std::string("test-device"));
        }
    }

    {
        HrtTlsStubServer server(
            [](const nlohmann::json &) {
                return HrtStubResponse{200, {{"status", "denied"}}};
            },
            "pairing-denied-stub");
        CHECK(server.ok());
        if (server.ok()) {
            HrtClientOptions options;
            options.timeout_s = 5;
            HrtClient probe(server.url(), options);
            options.expected_spki = probe.connect().spki_sha256;
            probe.close();
            HrtClient client(server.url(), options);
            client.connect();
            const HrtPairResult result = poll_hrt_pairing(client, sample_request());
            client.close();
            CHECK(result.status == HrtPairStatus::Denied);
            CHECK(result.token.empty());
        }
    }

    {
        HrtTlsStubServer server(
            [](const nlohmann::json &) {
                return HrtStubResponse{200, {{"status", "expired"}}};
            },
            "pairing-expired-stub");
        CHECK(server.ok());
        if (server.ok()) {
            HrtClientOptions options;
            options.timeout_s = 5;
            HrtClient probe(server.url(), options);
            options.expected_spki = probe.connect().spki_sha256;
            probe.close();
            HrtClient client(server.url(), options);
            client.connect();
            const HrtPairResult result = poll_hrt_pairing(client, sample_request());
            client.close();
            CHECK(result.status == HrtPairStatus::Expired);
        }
    }

    {
        HrtTlsStubServer server(
            [](const nlohmann::json &) {
                return HrtStubResponse{200, {{"status", "weird"}}};
            },
            "pairing-invalid-status-stub");
        CHECK(server.ok());
        if (server.ok()) {
            HrtClientOptions options;
            options.timeout_s = 5;
            HrtClient probe(server.url(), options);
            options.expected_spki = probe.connect().spki_sha256;
            probe.close();
            HrtClient client(server.url(), options);
            client.connect();
            CHECK_THROWS(poll_hrt_pairing(client, sample_request()), ToolError);
            client.close();
        }
    }

    {
        HrtTlsStubServer server(
            [](const nlohmann::json &) {
                return HrtStubResponse{200,
                                       {{"status", "approved"},
                                        {"user",
                                         {{"id", "u-123"}, {"username", "alice"}}}}};
            },
            "pairing-missing-token-stub");
        CHECK(server.ok());
        if (server.ok()) {
            HrtClientOptions options;
            options.timeout_s = 5;
            HrtClient probe(server.url(), options);
            options.expected_spki = probe.connect().spki_sha256;
            probe.close();
            HrtClient client(server.url(), options);
            client.connect();
            CHECK_THROWS(poll_hrt_pairing(client, sample_request()), ToolError);
            client.close();
        }
    }

    {
        /* Local input validation must fail before any frame is sent. */
        HrtTlsStubServer server(
            [](const nlohmann::json &) { return HrtStubResponse{}; },
            "pairing-local-validation-stub");
        CHECK(server.ok());
        if (server.ok()) {
            HrtClientOptions options;
            options.timeout_s = 5;
            HrtClient probe(server.url(), options);
            options.expected_spki = probe.connect().spki_sha256;
            probe.close();
            HrtClient client(server.url(), options);
            client.connect();
            CHECK_THROWS(start_hrt_pairing(client, "alice", "device", "cert", "nothex",
                                           "1.0"),
                         ToolError);
            CHECK_THROWS(start_hrt_pairing(client, "alice", "device", "",
                                           generate_hrt_code(), "1.0"),
                         ToolError);
            CHECK_THROWS(start_hrt_pairing(client, "", "device", "cert",
                                           generate_hrt_code(), "1.0"),
                         ToolError);
            CHECK_THROWS(start_hrt_pairing(client, "alice", "", "cert",
                                           generate_hrt_code(), "1.0"),
                         ToolError);
            client.close();
            CHECK(server.frames().empty());
        }
    }

    return test_result();
}
