#pragma once

/* A throwaway TLS 1.3 / ALPN hrt/1 server for client tests. It speaks the
   frame protocol and hands each decoded frame to a test callback; it never
   validates the client certificate, but records whether one was presented. */

#include "hrt_client.h"
#include "hrt_protocol.h"

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

struct HrtStubResponse {
    long status = 200;
    nlohmann::json body = nlohmann::json::object();
    nlohmann::json error; /* when set, sent as the frame's error object */
};

class HrtTlsStubServer {
public:
    using Handler = std::function<HrtStubResponse(const nlohmann::json &)>;

    explicit HrtTlsStubServer(Handler handler,
                              const std::string &name = "hrt-stub-server")
        : handler_(std::move(handler)),
          identity_(generate_hrt_device_identity(name)) {
        context_ = SSL_CTX_new(TLS_server_method());
        if (context_ == nullptr) {
            return;
        }
        SSL_CTX_set_min_proto_version(context_, TLS1_3_VERSION);
        SSL_CTX_set_verify(context_, SSL_VERIFY_NONE, nullptr);
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

    ~HrtTlsStubServer() {
        stopping_ = true;
        if (socket_ >= 0) {
            ::shutdown(socket_, SHUT_RDWR);
            ::close(socket_);
            socket_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        if (context_ != nullptr) {
            SSL_CTX_free(context_);
        }
    }

    HrtTlsStubServer(const HrtTlsStubServer &) = delete;
    HrtTlsStubServer &operator=(const HrtTlsStubServer &) = delete;

    bool ok() const { return socket_ >= 0 && context_ != nullptr; }
    unsigned short port() const { return port_; }
    const HrtDeviceIdentity &identity() const { return identity_; }
    bool saw_client_certificate() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return saw_client_certificate_;
    }
    std::string url() const {
        return "hrt://127.0.0.1:" + std::to_string(port_);
    }
    std::vector<nlohmann::json> frames() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return frames_;
    }

private:
    static int alpn_select(SSL *, const unsigned char **out, unsigned char *outlen,
                           const unsigned char *in, unsigned int inlen, void *) {
        static const unsigned char server_protocol[] = {5, 'h', 'r', 't', '/', '1'};
        if (SSL_select_next_proto(const_cast<unsigned char **>(out), outlen,
                                  server_protocol, sizeof(server_protocol), in,
                                  inlen) == OPENSSL_NPN_NEGOTIATED) {
            return SSL_TLSEXT_ERR_OK;
        }
        return SSL_TLSEXT_ERR_NOACK;
    }

    static bool write_all(SSL *ssl, const std::string &data) {
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

    static bool read_exact(SSL *ssl, unsigned char *data, size_t length) {
        size_t offset = 0;
        while (offset < length) {
            const int got =
                SSL_read(ssl, data + offset, static_cast<int>(length - offset));
            if (got <= 0) {
                return false;
            }
            offset += static_cast<size_t>(got);
        }
        return true;
    }

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
                if (peer != nullptr) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    saw_client_certificate_ = true;
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
            if (!read_exact(ssl, header, sizeof(header))) {
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
            if (!read_exact(ssl, reinterpret_cast<unsigned char *>(&payload[0]),
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
            {
                std::lock_guard<std::mutex> lock(mutex_);
                frames_.push_back(request);
            }
            HrtStubResponse response;
            try {
                response = handler_(request);
            } catch (const std::exception &) {
                return;
            }
            const std::string body =
                response.body.is_string() ? response.body.get<std::string>()
                                          : response.body.dump();
            nlohmann::json envelope{{"id", request.value("id", std::string{})}};
            if (!response.error.is_null()) {
                envelope["error"] = response.error;
            } else {
                envelope["status"] = response.status;
                envelope["headers"] = {{"content-type", "application/json"}};
                envelope["body_base64"] = base64_encode(body);
            }
            const std::string frame_out = encode_hrt_frame(envelope);
            if (!write_all(ssl, frame_out)) {
                return;
            }
        }
    }

    Handler handler_;
    HrtDeviceIdentity identity_;
    SSL_CTX *context_ = nullptr;
    int socket_ = -1;
    unsigned short port_ = 0;
    std::atomic<bool> stopping_{false};
    mutable std::mutex mutex_;
    bool saw_client_certificate_ = false;
    std::vector<nlohmann::json> frames_;
    std::thread thread_;
};
