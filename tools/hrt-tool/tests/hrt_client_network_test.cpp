#include "error.h"
#include "hrt_client.h"
#include "hrt_protocol.h"
#include "test_framework.h"

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int alpn_select(SSL *, const unsigned char **out, unsigned char *outlen,
                const unsigned char *in, unsigned int inlen, void *) {
    static const unsigned char server_protocol[] = {5, 'h', 'r', 't', '/', '1'};
    if (SSL_select_next_proto(const_cast<unsigned char **>(out), outlen,
                              server_protocol, sizeof(server_protocol), in,
                              inlen) == OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_OK;
    }
    return SSL_TLSEXT_ERR_NOACK;
}

int accept_certificate(int, X509_STORE_CTX *) { return 1; }

bool ssl_write_all(SSL *ssl, const std::string &data) {
    size_t offset = 0;
    while (offset < data.size()) {
        const int written = SSL_write(ssl, data.data() + offset,
                                      static_cast<int>(data.size() - offset));
        if (written <= 0) {
            return false;
        }
        offset += static_cast<size_t>(written);
    }
    return true;
}

bool ssl_read_exact(SSL *ssl, unsigned char *data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        const int got = SSL_read(ssl, data + offset,
                                 static_cast<int>(length - offset));
        if (got <= 0) {
            return false;
        }
        offset += static_cast<size_t>(got);
    }
    return true;
}

class TlsStubServer {
public:
    TlsStubServer() : identity_(generate_hrt_device_identity("hrt-stub-server")) {
        context_ = SSL_CTX_new(TLS_server_method());
        if (context_ == nullptr) {
            return;
        }
        SSL_CTX_set_min_proto_version(context_, TLS1_3_VERSION);
        SSL_CTX_set_verify(context_, SSL_VERIFY_PEER, accept_certificate);
        SSL_CTX_set_alpn_select_cb(context_, alpn_select, nullptr);
        BIO *cert_bio = BIO_new_mem_buf(identity_.certificate_pem.data(),
                                        static_cast<int>(identity_.certificate_pem.size()));
        X509 *certificate =
            cert_bio ? PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr) : nullptr;
        BIO_free(cert_bio);
        BIO *key_bio = BIO_new_mem_buf(identity_.private_key_pem.data(),
                                       static_cast<int>(identity_.private_key_pem.size()));
        EVP_PKEY *key =
            key_bio ? PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr) : nullptr;
        BIO_free(key_bio);
        const bool ok = certificate != nullptr && key != nullptr &&
                        SSL_CTX_use_certificate(context_, certificate) == 1 &&
                        SSL_CTX_use_PrivateKey(context_, key) == 1;
        X509_free(certificate);
        EVP_PKEY_free(key);
        if (!ok) {
            SSL_CTX_free(context_);
            context_ = nullptr;
            return;
        }
        socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (socket_ < 0) {
            return;
        }
        int one = 1;
        ::setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(socket_, reinterpret_cast<sockaddr *>(&address),
                   sizeof(address)) != 0) {
            ::close(socket_);
            socket_ = -1;
            return;
        }
        ::listen(socket_, 4);
        socklen_t length = sizeof(address);
        ::getsockname(socket_, reinterpret_cast<sockaddr *>(&address), &length);
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this] { serve(); });
    }

    ~TlsStubServer() {
        stopping_ = true;
        if (socket_ >= 0) {
            ::shutdown(socket_, SHUT_RDWR);
            ::close(socket_);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        if (context_ != nullptr) {
            SSL_CTX_free(context_);
        }
    }

    bool ok() const { return socket_ >= 0 && context_ != nullptr; }
    unsigned short port() const { return port_; }
    const HrtDeviceIdentity &identity() const { return identity_; }
    bool saw_client_certificate() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return saw_client_certificate_;
    }

private:
    void serve() {
        while (!stopping_) {
            const int client = ::accept(socket_, nullptr, nullptr);
            if (client < 0) {
                if (stopping_) break;
                continue;
            }
            SSL *ssl = SSL_new(context_);
            if (ssl == nullptr || SSL_set_fd(ssl, client) != 1) {
                SSL_free(ssl);
                ::close(client);
                continue;
            }
            if (SSL_accept(ssl) == 1) {
                X509 *peer = SSL_get1_peer_certificate(ssl);
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    saw_client_certificate_ = peer != nullptr;
                }
                X509_free(peer);
                handle(ssl);
            }
            SSL_shutdown(ssl);
            SSL_free(ssl);
            ::close(client);
        }
    }

    void handle(SSL *ssl) {
        while (!stopping_) {
            unsigned char header[4] = {};
            if (!ssl_read_exact(ssl, header, sizeof(header))) {
                return;
            }
            const uint32_t length = (static_cast<uint32_t>(header[0]) << 24) |
                                    (static_cast<uint32_t>(header[1]) << 16) |
                                    (static_cast<uint32_t>(header[2]) << 8) |
                                    static_cast<uint32_t>(header[3]);
            if (length == 0 || length > kHrtMaxFrameBytes) {
                return;
            }
            std::string payload(length, '\0');
            if (!ssl_read_exact(ssl, reinterpret_cast<unsigned char *>(&payload[0]),
                                payload.size())) {
                return;
            }
            std::string frame(reinterpret_cast<const char *>(header), sizeof(header));
            frame += payload;
            nlohmann::json request;
            try {
                request = decode_hrt_frame(frame);
            } catch (const ToolError &) {
                return;
            }
            const std::string id = request.at("id").get<std::string>();
            nlohmann::json body = nlohmann::json::object();
            std::string status = "200 OK";
            if (request.value("op", std::string{}) == "ping") {
                body["protocol"] = "hrt/1";
            } else if (request.value("op", std::string{}) == "request") {
                body["method"] = request.value("method", std::string{});
                body["path"] = request.value("path", std::string{});
                body["clientCert"] = saw_client_certificate();
            } else if (request.value("op", std::string{}) == "pair.start") {
                body["request_id"] = std::string(32, 'a');
                body["poll_secret"] = std::string(64, 'b');
                body["expires_at"] = 1760000000;
                body["verification_url"] = "https://example.test/settings";
            } else {
                status = "404 Not Found";
            }
            const std::string response =
                encode_hrt_frame({{"id", id},
                                  {"status", 200},
                                  {"headers", {{"content-type", "application/json"}}},
                                  {"body_base64", base64_encode(body.dump())}});
            (void)status;
            if (!ssl_write_all(ssl, response)) {
                return;
            }
        }
    }

    HrtDeviceIdentity identity_;
    SSL_CTX *context_ = nullptr;
    int socket_ = -1;
    unsigned short port_ = 0;
    std::atomic<bool> stopping_{false};
    mutable std::mutex mutex_;
    bool saw_client_certificate_ = false;
    std::thread thread_;
};

/* A one-shot SOCKS5 relay that records the CONNECT target and forwards the
   byte stream to the TLS stub. */
class Socks5Relay {
public:
    explicit Socks5Relay(unsigned short backend_port) : backend_port_(backend_port) {
        socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (socket_ < 0) return;
        int one = 1;
        ::setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(socket_, reinterpret_cast<sockaddr *>(&address),
                   sizeof(address)) != 0) {
            ::close(socket_);
            socket_ = -1;
            return;
        }
        ::listen(socket_, 4);
        socklen_t length = sizeof(address);
        ::getsockname(socket_, reinterpret_cast<sockaddr *>(&address), &length);
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this] { serve(); });
    }

    ~Socks5Relay() {
        stopping_ = true;
        if (socket_ >= 0) {
            ::shutdown(socket_, SHUT_RDWR);
            ::close(socket_);
        }
        if (thread_.joinable()) thread_.join();
    }

    bool ok() const { return socket_ >= 0; }
    unsigned short port() const { return port_; }
    std::string target() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return target_;
    }

private:
    static bool read_exact(int fd, unsigned char *data, size_t size) {
        size_t offset = 0;
        while (offset < size) {
            const ssize_t got = ::recv(fd, data + offset, size - offset, 0);
            if (got <= 0) return false;
            offset += static_cast<size_t>(got);
        }
        return true;
    }

    static bool write_exact(int fd, const unsigned char *data, size_t size) {
        size_t offset = 0;
        while (offset < size) {
            const ssize_t written = ::send(fd, data + offset, size - offset, MSG_NOSIGNAL);
            if (written <= 0) return false;
            offset += static_cast<size_t>(written);
        }
        return true;
    }

    void serve() {
        const int client = ::accept(socket_, nullptr, nullptr);
        if (client < 0) return;
        unsigned char greeting[2] = {};
        if (!read_exact(client, greeting, 2) || greeting[0] != 0x05) {
            ::close(client);
            return;
        }
        std::string methods(greeting[1], '\0');
        if (greeting[1] > 0 &&
            !read_exact(client, reinterpret_cast<unsigned char *>(&methods[0]),
                        methods.size())) {
            ::close(client);
            return;
        }
        const unsigned char no_auth[2] = {0x05, 0x00};
        if (!write_exact(client, no_auth, 2)) {
            ::close(client);
            return;
        }
        unsigned char header[4] = {};
        if (!read_exact(client, header, 4) || header[3] != 0x03) {
            ::close(client);
            return;
        }
        unsigned char label_length = 0;
        if (!read_exact(client, &label_length, 1)) {
            ::close(client);
            return;
        }
        std::string host(label_length, '\0');
        if (label_length > 0 &&
            !read_exact(client, reinterpret_cast<unsigned char *>(&host[0]),
                        host.size())) {
            ::close(client);
            return;
        }
        unsigned char port_bytes[2] = {};
        if (!read_exact(client, port_bytes, 2)) {
            ::close(client);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            target_ = host;
        }
        const int backend = ::socket(AF_INET, SOCK_STREAM, 0);
        if (backend < 0) {
            ::close(client);
            return;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(backend_port_);
        if (::connect(backend, reinterpret_cast<sockaddr *>(&address),
                      sizeof(address)) != 0) {
            ::close(backend);
            ::close(client);
            return;
        }
        const unsigned char success[10] = {0x05, 0x00, 0x00, 0x01, 0,
                                           0,    0,    0,    0,    0};
        if (!write_exact(client, success, sizeof(success))) {
            ::close(backend);
            ::close(client);
            return;
        }
        while (!stopping_) {
            pollfd descriptors[2] = {{client, POLLIN, 0}, {backend, POLLIN, 0}};
            const int ready = ::poll(descriptors, 2, 1000);
            if (ready < 0) break;
            if (ready == 0) continue;
            for (int index = 0; index < 2; ++index) {
                if ((descriptors[index].revents & (POLLIN | POLLHUP)) == 0) continue;
                char buffer[4096];
                const ssize_t got = ::recv(descriptors[index].fd, buffer, sizeof(buffer), 0);
                if (got <= 0) {
                    stopping_ = true;
                    break;
                }
                const int other = 1 - index;
                if (!write_exact(descriptors[other].fd,
                                 reinterpret_cast<unsigned char *>(buffer),
                                 static_cast<size_t>(got))) {
                    stopping_ = true;
                    break;
                }
            }
        }
        ::close(backend);
        ::close(client);
    }

    unsigned short backend_port_;
    int socket_ = -1;
    unsigned short port_ = 0;
    std::atomic<bool> stopping_{false};
    mutable std::mutex mutex_;
    std::string target_;
    std::thread thread_;
};

} // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    {
        TlsStubServer server;
        CHECK(server.ok());
        if (server.ok()) {
            HrtClientOptions options;
            options.timeout_s = 5;
            const std::string url =
                "hrt://127.0.0.1:" + std::to_string(server.port());
            HrtClient probe(url, options);
            const HrtServerCertificate certificate = probe.connect();
            CHECK_EQ(certificate.spki_sha256.size(), static_cast<size_t>(64));
            CHECK(!certificate.subject.empty());
            probe.close();

            options.expected_spki = certificate.spki_sha256;
            HrtClient pinned(url, options);
            pinned.connect();
            const nlohmann::json response =
                pinned.call({{"op", "ping"}});
            CHECK_EQ(response.value("status", 0), 200);
            const nlohmann::json body = nlohmann::json::parse(
                base64_decode(response.value("body_base64", std::string{})));
            CHECK_EQ(body.value("protocol", std::string{}), std::string("hrt/1"));
            pinned.close();

            options.expected_spki = std::string(64, '0');
            HrtClient wrong_pin(url, options);
            CHECK_THROWS(wrong_pin.connect(), ToolError);
        }
    }

    {
        TlsStubServer server;
        CHECK(server.ok());
        if (server.ok()) {
            HrtClientOptions probe_options;
            probe_options.timeout_s = 5;
            const std::string url =
                "hrt://127.0.0.1:" + std::to_string(server.port());
            HrtClient probe(url, probe_options);
            const HrtServerCertificate certificate = probe.connect();
            probe.close();

            const HrtDeviceIdentity identity =
                generate_hrt_device_identity("hrt-client-test");
            HrtClientOptions options;
            options.timeout_s = 5;
            options.expected_spki = certificate.spki_sha256;
            options.client_cert_pem = identity.certificate_pem;
            options.client_key_pem = identity.private_key_pem;
            HrtClient client(url, options);
            client.connect();
            const nlohmann::json response = client.request(
                "GET", "/api/tool/v1/me", {{"authorization", "Bearer test"}}, "");
            const nlohmann::json body = nlohmann::json::parse(
                base64_decode(response.value("body_base64", std::string{})));
            CHECK_EQ(body.value("method", std::string{}), std::string("GET"));
            CHECK_EQ(body.value("path", std::string{}),
                     std::string("/api/tool/v1/me"));
            CHECK(body.value("clientCert", false));
            CHECK(server.saw_client_certificate());
            client.close();
        }
    }

    {
        TlsStubServer server;
        CHECK(server.ok());
        if (server.ok()) {
            HrtClientOptions probe_options;
            probe_options.timeout_s = 5;
            const std::string url =
                "hrt://127.0.0.1:" + std::to_string(server.port());
            HrtClient probe(url, probe_options);
            const HrtServerCertificate certificate = probe.connect();
            probe.close();

            Socks5Relay relay(server.port());
            CHECK(relay.ok());
            if (relay.ok()) {
                HrtClientOptions options;
                options.timeout_s = 5;
                options.proxy = "socks5h://127.0.0.1:" +
                                std::to_string(relay.port());
                options.expected_spki = certificate.spki_sha256;
                HrtClient client(url, options);
                client.connect();
                const nlohmann::json response = client.call({{"op", "ping"}});
                CHECK_EQ(response.value("status", 0), 200);
                CHECK_EQ(relay.target(), std::string("127.0.0.1"));
                client.close();
            }
        }
    }

    return test_result();
}
