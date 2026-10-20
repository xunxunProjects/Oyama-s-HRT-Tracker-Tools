#include "hrt_client.h"

#include "error.h"
#include "hrt_protocol.h"
#include "http.h"
#include "net_error.h"

#include <openssl/asn1.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

std::string trim_copy(const std::string &value) {
    std::string::size_type begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    std::string::size_type end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool has_control_character(const std::string &value) {
    for (unsigned char c : value) {
        if (c < 0x20 || c == 0x7f) {
            return true;
        }
    }
    return false;
}

std::string hex_encode(const unsigned char *data, size_t length) {
    static const char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        result.push_back(digits[data[i] >> 4]);
        result.push_back(digits[data[i] & 0x0f]);
    }
    return result;
}

std::string bio_to_string(BIO *bio) {
    char *data = nullptr;
    const long length = BIO_get_mem_data(bio, &data);
    if (length <= 0 || data == nullptr) {
        return {};
    }
    return std::string(data, static_cast<size_t>(length));
}

std::string x509_name_string(X509_NAME *name) {
    if (name == nullptr) {
        return {};
    }
    BIO *bio = BIO_new(BIO_s_mem());
    if (bio == nullptr) {
        return {};
    }
    std::string result;
    if (X509_NAME_print_ex(bio, name, 0, XN_FLAG_RFC2253) >= 0) {
        result = bio_to_string(bio);
    }
    BIO_free(bio);
    return result;
}

std::string asn1_time_string(const ASN1_TIME *time) {
    if (time == nullptr) {
        return {};
    }
    BIO *bio = BIO_new(BIO_s_mem());
    if (bio == nullptr) {
        return {};
    }
    std::string result;
    if (ASN1_TIME_print(bio, time) == 1) {
        result = bio_to_string(bio);
    }
    BIO_free(bio);
    return result;
}

std::string certificate_spki_hex(X509 *certificate) {
    if (certificate == nullptr) {
        return {};
    }
    X509_PUBKEY *public_key = X509_get_X509_PUBKEY(certificate);
    if (public_key == nullptr) {
        return {};
    }
    unsigned char *der = nullptr;
    const int der_length = i2d_X509_PUBKEY(public_key, &der);
    if (der_length <= 0 || der == nullptr) {
        OPENSSL_free(der);
        return {};
    }
    unsigned char digest[EVP_MAX_MD_SIZE] = {};
    unsigned int digest_length = 0;
    std::string result;
    if (EVP_Digest(der, static_cast<size_t>(der_length), digest, &digest_length,
                   EVP_sha256(), nullptr) == 1) {
        result = hex_encode(digest, digest_length);
    }
    OPENSSL_free(der);
    return result;
}

std::string openssl_error() {
    const unsigned long code = ERR_get_error();
    if (code == 0) {
        return "TLS error";
    }
    char buffer[256] = {};
    ERR_error_string_n(code, buffer, sizeof(buffer));
    return buffer;
}

#ifndef _WIN32

void set_socket_timeouts(int fd, long timeout_s) {
    timeval value{};
    value.tv_sec = timeout_s;
    value.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &value, sizeof(value));
}

int connect_tcp(const std::string &host, const std::string &port, long timeout_s) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *addresses = nullptr;
    const int resolved = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses);
    if (resolved != 0 || addresses == nullptr) {
        throw NetworkFailure("HRT_DNS", "cannot resolve " + host,
                             FailureKind::FinalFallback);
    }
    int connected = -1;
    int last_error = 0;
    for (addrinfo *address = addresses; address != nullptr;
         address = address->ai_next) {
        const int fd = ::socket(address->ai_family, address->ai_socktype,
                                address->ai_protocol);
        if (fd < 0) {
            last_error = errno;
            continue;
        }
        const int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int result = ::connect(fd, address->ai_addr, address->ai_addrlen);
        if (result != 0) {
            last_error = errno;
        }
        if (result != 0 && errno == EINPROGRESS) {
            pollfd wait{};
            wait.fd = fd;
            wait.events = POLLOUT;
            const int timeout_ms = static_cast<int>(timeout_s * 1000);
            result = ::poll(&wait, 1, timeout_ms);
            if (result > 0) {
                int error = 0;
                socklen_t length = sizeof(error);
                ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length);
                result = error == 0 ? 0 : -1;
                if (error != 0) {
                    last_error = error;
                }
            } else {
                result = -1;
                last_error = ETIMEDOUT;
            }
        }
        if (result == 0) {
            ::fcntl(fd, F_SETFL, flags);
            set_socket_timeouts(fd, timeout_s);
            connected = fd;
            break;
        }
        ::close(fd);
    }
    ::freeaddrinfo(addresses);
    if (connected < 0) {
        FailureKind kind = classify_errno(last_error);
        if (kind == FailureKind::None) {
            kind = FailureKind::TunnelInterrupted;
        }
        throw NetworkFailure("HRT_CONNECT",
                             "cannot connect to " + host + ":" + port, kind);
    }
    return connected;
}

bool write_exact(int fd, const unsigned char *data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        const ssize_t written = ::send(fd, data + offset, length - offset, MSG_NOSIGNAL);
        if (written <= 0) {
            return false;
        }
        offset += static_cast<size_t>(written);
    }
    return true;
}

bool read_exact_fd(int fd, unsigned char *data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        const ssize_t got = ::recv(fd, data + offset, length - offset, 0);
        if (got <= 0) {
            return false;
        }
        offset += static_cast<size_t>(got);
    }
    return true;
}

int socks5_connect(const std::string &proxy, const std::string &host,
                   const std::string &port, long timeout_s) {
    const Url proxy_url = parse_url(proxy);
    if (proxy_url.scheme != "socks5h") {
        throw ToolError(ExitCode::Usage, "PROXY_URL",
                        "the raw transport requires a socks5h:// proxy");
    }
    int fd = 0;
    try {
        fd = connect_tcp(proxy_url.host, proxy_url.port, timeout_s);
    } catch (const ToolError &error) {
        throw NetworkFailure(
            "HRT_PROXY",
            "cannot connect to the SOCKS5 proxy: " + std::string(error.what()),
            FailureKind::ProxyDown);
    }
    const unsigned char greeting[3] = {0x05, 0x01, 0x00};
    if (!write_exact(fd, greeting, sizeof(greeting))) {
        ::close(fd);
        throw NetworkFailure("HRT_PROXY", "cannot write the SOCKS5 greeting",
                             FailureKind::TunnelInterrupted);
    }
    unsigned char method[2] = {};
    if (!read_exact_fd(fd, method, sizeof(method)) || method[0] != 0x05 ||
        method[1] != 0x00) {
        ::close(fd);
        throw NetworkFailure("HRT_PROXY",
                             "the SOCKS5 proxy refused authentication",
                             FailureKind::ProxyDown);
    }
    if (host.empty() || host.size() > 255) {
        ::close(fd);
        throw ToolError(ExitCode::Usage, "HRT_HOST", "invalid target host");
    }
    const unsigned long numeric_port = std::stoul(port);
    std::string request;
    request.push_back(0x05);
    request.push_back(0x01);
    request.push_back(0x00);
    request.push_back(0x03);
    request.push_back(static_cast<char>(host.size()));
    request += host;
    request.push_back(static_cast<char>((numeric_port >> 8) & 0xff));
    request.push_back(static_cast<char>(numeric_port & 0xff));
    if (!write_exact(fd, reinterpret_cast<const unsigned char *>(request.data()),
                     request.size())) {
        ::close(fd);
        throw NetworkFailure("HRT_PROXY", "cannot write the SOCKS5 request",
                             FailureKind::TunnelInterrupted);
    }
    unsigned char reply[4] = {};
    if (!read_exact_fd(fd, reply, sizeof(reply)) || reply[0] != 0x05 ||
        reply[1] != 0x00) {
        ::close(fd);
        FailureKind kind = classify_socks5(reply[1]);
        if (kind == FailureKind::None) {
            kind = FailureKind::TunnelInterrupted;
        }
        throw NetworkFailure("HRT_PROXY",
                             "the SOCKS5 proxy could not reach the target", kind);
    }
    size_t extra = 0;
    if (reply[3] == 0x01) {
        extra = 4;
    } else if (reply[3] == 0x04) {
        extra = 16;
    } else if (reply[3] == 0x03) {
        unsigned char length = 0;
        if (!read_exact_fd(fd, &length, 1)) {
            ::close(fd);
            throw NetworkFailure("HRT_PROXY", "truncated SOCKS5 reply",
                                 FailureKind::TunnelInterrupted);
        }
        extra = length;
    } else {
        ::close(fd);
        throw NetworkFailure("HRT_PROXY", "invalid SOCKS5 reply",
                             FailureKind::TunnelInterrupted);
    }
    unsigned char discard[256 + 2] = {};
    if (extra + 2 > sizeof(discard) ||
        !read_exact_fd(fd, discard, extra + 2)) {
        ::close(fd);
        throw NetworkFailure("HRT_PROXY", "truncated SOCKS5 reply",
                             FailureKind::TunnelInterrupted);
    }
    return fd;
}

#endif

} // namespace

std::string sha256_hex(const std::string &data) {
    unsigned char digest[EVP_MAX_MD_SIZE] = {};
    unsigned int length = 0;
    if (EVP_Digest(data.data(), data.size(), digest, &length, EVP_sha256(),
                   nullptr) != 1) {
        throw ToolError(ExitCode::Data, "HASH", "cannot compute SHA-256");
    }
    return hex_encode(digest, length);
}

std::string base64_encode(const std::string &data) {
    if (data.empty()) {
        return {};
    }
    const size_t capacity = 4 * ((data.size() + 2) / 3) + 1;
    std::string result(capacity, '\0');
    const int length = EVP_EncodeBlock(
        reinterpret_cast<unsigned char *>(&result[0]),
        reinterpret_cast<const unsigned char *>(data.data()),
        static_cast<int>(data.size()));
    if (length < 0) {
        throw ToolError(ExitCode::Data, "BASE64", "cannot encode data");
    }
    result.resize(static_cast<size_t>(length));
    return result;
}

std::string base64_decode(const std::string &data) {
    if (data.empty()) {
        return {};
    }
    if (data.size() % 4 != 0) {
        throw ToolError(ExitCode::Data, "BASE64", "invalid base64 length");
    }
    size_t padding = 0;
    if (!data.empty() && data.back() == '=') {
        ++padding;
        if (data.size() >= 2 && data[data.size() - 2] == '=') {
            ++padding;
        }
    }
    std::string result(data.size() / 4 * 3 + 1, '\0');
    const int decoded = EVP_DecodeBlock(
        reinterpret_cast<unsigned char *>(&result[0]),
        reinterpret_cast<const unsigned char *>(data.data()),
        static_cast<int>(data.size()));
    if (decoded < 0 || static_cast<size_t>(decoded) < padding) {
        throw ToolError(ExitCode::Data, "BASE64", "invalid base64 data");
    }
    result.resize(static_cast<size_t>(decoded) - padding);
    return result;
}

namespace {

bool signing_field_is_safe(const std::string &value) {
    return value.find('\n') == std::string::npos &&
           value.find('\r') == std::string::npos;
}

std::string lowercase_header(const std::map<std::string, std::string> &headers,
                             const std::string &name) {
    const auto found = headers.find(name);
    if (found == headers.end()) {
        return {};
    }
    if (!signing_field_is_safe(found->second)) {
        throw ToolError(ExitCode::Usage, "HRT_SIGNATURE",
                        "a request header contains a newline");
    }
    return found->second;
}

} // namespace

std::string hrt_request_signing_string(
    const std::string &method, const std::string &path, const std::string &body,
    const std::map<std::string, std::string> &headers) {
    if (method.empty() || !signing_field_is_safe(method)) {
        throw ToolError(ExitCode::Usage, "HRT_SIGNATURE",
                        "invalid request method for signing");
    }
    if (path.empty() || path.front() != '/' || !signing_field_is_safe(path)) {
        throw ToolError(ExitCode::Usage, "HRT_SIGNATURE",
                        "invalid request path for signing");
    }
    return "hrt-request-v1\n" + method + "\n" + path + "\n" + sha256_hex(body) +
           "\n" + lowercase_header(headers, "authorization") + "\n" +
           lowercase_header(headers, "if-match");
}

std::string hrt_sign_request(const std::string &private_key_pem,
                             const std::string &canonical) {
    BIO *key_bio =
        BIO_new_mem_buf(private_key_pem.data(), static_cast<int>(private_key_pem.size()));
    EVP_PKEY *key =
        key_bio ? PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr) : nullptr;
    BIO_free(key_bio);
    EVP_MD_CTX *context = EVP_MD_CTX_new();
    std::string signature;
    if (key != nullptr && context != nullptr &&
        EVP_DigestSignInit(context, nullptr, EVP_sha256(), nullptr, key) == 1 &&
        EVP_DigestSignUpdate(context, canonical.data(), canonical.size()) == 1) {
        size_t length = 0;
        if (EVP_DigestSignFinal(context, nullptr, &length) == 1 && length > 0) {
            std::string der(length, '\0');
            if (EVP_DigestSignFinal(context, reinterpret_cast<unsigned char *>(&der[0]),
                                    &length) == 1) {
                der.resize(length);
                signature = base64_encode(der);
            }
        }
    }
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    if (signature.empty()) {
        throw ToolError(ExitCode::Data, "HRT_SIGNATURE",
                        "cannot sign the request with the device key");
    }
    return signature;
}

std::string generate_hrt_code() {
    unsigned char bytes[64] = {};
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        throw ToolError(ExitCode::Data, "RANDOM", "cannot generate a pairing code");
    }
    return hex_encode(bytes, sizeof(bytes));
}

HrtDeviceIdentity generate_hrt_device_identity(const std::string &device_name) {
    const std::string name = trim_copy(device_name);
    if (name.empty() || name.size() > 64 || has_control_character(name)) {
        throw ToolError(ExitCode::Usage, "DEVICE_NAME",
                        "device name must be 1..64 printable characters");
    }

    EVP_PKEY_CTX *key_context = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    EVP_PKEY *key = nullptr;
    X509 *certificate = nullptr;
    if (key_context == nullptr || EVP_PKEY_keygen_init(key_context) != 1 ||
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(key_context,
                                               NID_X9_62_prime256v1) != 1 ||
        EVP_PKEY_keygen(key_context, &key) != 1 || key == nullptr) {
        EVP_PKEY_CTX_free(key_context);
        EVP_PKEY_free(key);
        throw ToolError(ExitCode::Data, "HRT_IDENTITY", "cannot generate a device key");
    }
    EVP_PKEY_CTX_free(key_context);

    certificate = X509_new();
    if (certificate == nullptr || X509_set_version(certificate, 2) != 1) {
        X509_free(certificate);
        EVP_PKEY_free(key);
        throw ToolError(ExitCode::Data, "HRT_IDENTITY", "cannot create a device certificate");
    }
    unsigned char serial_bytes[4] = {};
    if (RAND_bytes(serial_bytes, sizeof(serial_bytes)) != 1) {
        X509_free(certificate);
        EVP_PKEY_free(key);
        throw ToolError(ExitCode::Data, "HRT_IDENTITY", "cannot generate a serial number");
    }
    const long serial =
        (static_cast<long>(serial_bytes[0]) << 24 |
         static_cast<long>(serial_bytes[1]) << 16 |
         static_cast<long>(serial_bytes[2]) << 8 |
         static_cast<long>(serial_bytes[3])) &
        0x7fffffffL;
    X509_gmtime_adj(X509_getm_notBefore(certificate), -60);
    X509_gmtime_adj(X509_getm_notAfter(certificate), 365L * 24 * 60 * 60);
    ASN1_INTEGER_set(X509_get_serialNumber(certificate), serial);
    X509_set_pubkey(certificate, key);
    X509_NAME *subject = X509_get_subject_name(certificate);
    X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>(name.data()),
                               static_cast<int>(name.size()), -1, 0);
    X509_set_issuer_name(certificate, subject);
    if (X509_sign(certificate, key, EVP_sha256()) <= 0) {
        X509_free(certificate);
        EVP_PKEY_free(key);
        throw ToolError(ExitCode::Data, "HRT_IDENTITY", "cannot sign the device certificate");
    }

    BIO *cert_bio = BIO_new(BIO_s_mem());
    BIO *key_bio = BIO_new(BIO_s_mem());
    if (cert_bio == nullptr || key_bio == nullptr) {
        BIO_free(cert_bio);
        BIO_free(key_bio);
        X509_free(certificate);
        EVP_PKEY_free(key);
        throw ToolError(ExitCode::Data, "HRT_IDENTITY", "cannot encode the device identity");
    }
    HrtDeviceIdentity identity;
    if (PEM_write_bio_X509(cert_bio, certificate) == 1) {
        identity.certificate_pem = bio_to_string(cert_bio);
    }
    if (PEM_write_bio_PrivateKey(key_bio, key, nullptr, nullptr, 0, nullptr,
                                 nullptr) == 1) {
        identity.private_key_pem = bio_to_string(key_bio);
    }
    BIO_free(cert_bio);
    BIO_free(key_bio);

    unsigned char *der = nullptr;
    const int der_length = i2d_X509(certificate, &der);
    if (der_length <= 0 || der == nullptr) {
        OPENSSL_free(der);
        X509_free(certificate);
        EVP_PKEY_free(key);
        throw ToolError(ExitCode::Data, "HRT_IDENTITY", "cannot encode the certificate");
    }
    identity.fingerprint =
        sha256_hex(std::string(reinterpret_cast<const char *>(der),
                               static_cast<size_t>(der_length)));
    OPENSSL_free(der);
    X509_free(certificate);
    EVP_PKEY_free(key);
    if (identity.certificate_pem.empty() || identity.private_key_pem.empty()) {
        throw ToolError(ExitCode::Data, "HRT_IDENTITY", "cannot encode the device identity");
    }
    return identity;
}

bool is_hrt_url(const std::string &url) {
    std::string lower = url;
    for (char &c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower.rfind("hrt://", 0) == 0;
}

std::string hrt_url_host(const std::string &url) {
    if (!is_hrt_url(url)) {
        throw ToolError(ExitCode::Usage, "HRT_URL", "server must use hrt://");
    }
    const Url parsed = parse_url(url);
    if (parsed.scheme != "hrt") {
        throw ToolError(ExitCode::Usage, "HRT_URL", "server must use hrt://");
    }
    return parsed.host;
}

std::string hrt_url_port(const std::string &url) {
    if (!is_hrt_url(url)) {
        throw ToolError(ExitCode::Usage, "HRT_URL", "server must use hrt://");
    }
    const Url parsed = parse_url(url);
    if (parsed.port.empty()) {
        return "9443";
    }
    return parsed.port;
}

std::string hrt_https_fallback(const std::string &url) {
    const std::string host = hrt_url_host(url);
    const bool ipv6 = host.find(':') != std::string::npos;
    return "https://" + (ipv6 ? "[" + host + "]" : host);
}

struct HrtClient::Impl {
    std::string host;
    std::string port;
    HrtClientOptions options;
    int fd = -1;
    SSL_CTX *context = nullptr;
    SSL *ssl = nullptr;
    HrtServerCertificate certificate;
    unsigned long next_id = 0;
    bool connected = false;

    ~Impl() { close(); }

    void close() {
        if (ssl != nullptr) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            ssl = nullptr;
        }
        if (context != nullptr) {
            SSL_CTX_free(context);
            context = nullptr;
        }
#ifndef _WIN32
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
#endif
        connected = false;
    }
};

HrtClient::HrtClient(std::string url, HrtClientOptions options)
    : impl_(new Impl) {
    if (!is_hrt_url(url)) {
        throw ToolError(ExitCode::Usage, "HRT_URL", "server must use hrt://");
    }
    const Url parsed = parse_url(url);
    if (!parsed.path.empty() && parsed.path != "/") {
        throw ToolError(ExitCode::Usage, "HRT_URL",
                        "an hrt:// server URL has no path");
    }
    impl_->host = parsed.host;
    impl_->port = parsed.port.empty() ? "9443" : parsed.port;
    impl_->options = std::move(options);
    if ((impl_->options.client_cert_pem.empty()) !=
        (impl_->options.client_key_pem.empty())) {
        throw ToolError(ExitCode::Usage, "HRT_IDENTITY",
                        "device certificate and key must be provided together");
    }
}

HrtClient::~HrtClient() = default;

HrtServerCertificate HrtClient::connect() {
#ifdef _WIN32
    throw ToolError(ExitCode::NotImplemented, "NOT_IMPLEMENTED",
                    "the raw hrt transport is not available on Windows yet");
#else
    impl_->close();
    const bool hidden =
        impl_->host.size() > 6 &&
        (impl_->host.compare(impl_->host.size() - 6, 6, ".onion") == 0 ||
         (impl_->host.size() > 4 &&
          impl_->host.compare(impl_->host.size() - 4, 4, ".i2p") == 0));
    if (hidden && impl_->options.proxy.empty()) {
        throw ToolError(ExitCode::Network, "PROXY_MISSING",
                        "hidden-service names require a socks5h proxy");
    }
    impl_->fd = impl_->options.proxy.empty()
                    ? connect_tcp(impl_->host, impl_->port, impl_->options.timeout_s)
                    : socks5_connect(impl_->options.proxy, impl_->host, impl_->port,
                                     impl_->options.timeout_s);

    impl_->context = SSL_CTX_new(TLS_client_method());
    if (impl_->context == nullptr) {
        impl_->close();
        throw ToolError(ExitCode::Network, "HRT_TLS", "cannot create a TLS context");
    }
    SSL_CTX_set_min_proto_version(impl_->context, TLS1_3_VERSION);
    SSL_CTX_set_verify(impl_->context, SSL_VERIFY_NONE, nullptr);

    impl_->ssl = SSL_new(impl_->context);
    if (impl_->ssl == nullptr || SSL_set_fd(impl_->ssl, impl_->fd) != 1) {
        impl_->close();
        throw ToolError(ExitCode::Network, "HRT_TLS", "cannot create the TLS session");
    }
    SSL_set_tlsext_host_name(impl_->ssl, impl_->host.c_str());
    static const unsigned char kAlpn[] = {5, 'h', 'r', 't', '/', '1'};
    if (SSL_set_alpn_protos(impl_->ssl, kAlpn, sizeof(kAlpn)) != 0) {
        impl_->close();
        throw ToolError(ExitCode::Network, "HRT_TLS", "cannot select hrt/1");
    }
    ERR_clear_error();
    if (SSL_connect(impl_->ssl) != 1) {
        const std::string detail = openssl_error();
        impl_->close();
        throw NetworkFailure("HRT_TLS", "TLS handshake failed: " + detail,
                             FailureKind::TunnelInterrupted);
    }
    const unsigned char *selected = nullptr;
    unsigned int selected_length = 0;
    SSL_get0_alpn_selected(impl_->ssl, &selected, &selected_length);
    if (selected == nullptr || selected_length != 5 ||
        std::memcmp(selected, "hrt/1", 5) != 0) {
        impl_->close();
        throw ToolError(ExitCode::Network, "HRT_ALPN",
                        "the server did not negotiate hrt/1");
    }

    X509 *peer = SSL_get1_peer_certificate(impl_->ssl);
    if (peer == nullptr) {
        impl_->close();
        throw ToolError(ExitCode::Network, "HRT_TLS", "the server presented no certificate");
    }
    impl_->certificate.spki_sha256 = certificate_spki_hex(peer);
    impl_->certificate.subject = x509_name_string(X509_get_subject_name(peer));
    impl_->certificate.issuer = x509_name_string(X509_get_issuer_name(peer));
    impl_->certificate.not_before = asn1_time_string(X509_get0_notBefore(peer));
    impl_->certificate.not_after = asn1_time_string(X509_get0_notAfter(peer));
    X509_free(peer);
    if (impl_->certificate.spki_sha256.empty()) {
        impl_->close();
        throw ToolError(ExitCode::Network, "HRT_TLS",
                        "cannot compute the server public-key pin");
    }
    if (!impl_->options.expected_spki.empty() &&
        impl_->certificate.spki_sha256 != impl_->options.expected_spki) {
        impl_->close();
        throw ToolError(ExitCode::Trust, "TRUST_CHANGED",
                        "the raw server public-key pin changed");
    }
    impl_->connected = true;
    return impl_->certificate;
#endif
}

void HrtClient::close() { impl_->close(); }

nlohmann::json HrtClient::call(const nlohmann::json &message) {
#ifdef _WIN32
    throw ToolError(ExitCode::NotImplemented, "NOT_IMPLEMENTED",
                    "the raw hrt transport is not available on Windows yet");
#else
    if (!impl_->connected || impl_->ssl == nullptr) {
        throw ToolError(ExitCode::Network, "HRT_TLS", "the raw connection is not open");
    }
    if (impl_->options.expected_spki.empty()) {
        throw ToolError(ExitCode::Usage, "HRT_TRUST",
                        "the raw connection is not pinned");
    }
    nlohmann::json envelope = message;
    const std::string id = std::to_string(++impl_->next_id);
    envelope["id"] = id;
    const std::string frame = encode_hrt_frame(envelope);
    size_t offset = 0;
    while (offset < frame.size()) {
        const int written = SSL_write(impl_->ssl, frame.data() + offset,
                                      static_cast<int>(frame.size() - offset));
        if (written <= 0) {
            throw NetworkFailure("HRT_IO", "cannot write the hrt frame",
                                 FailureKind::TunnelInterrupted);
        }
        offset += static_cast<size_t>(written);
    }
    unsigned char header[4] = {};
    size_t read = 0;
    while (read < sizeof(header)) {
        const int got = SSL_read(impl_->ssl, header + read,
                                 static_cast<int>(sizeof(header) - read));
        if (got <= 0) {
            throw NetworkFailure("HRT_IO", "cannot read the hrt frame",
                                 FailureKind::TunnelInterrupted);
        }
        read += static_cast<size_t>(got);
    }
    const uint32_t length = (static_cast<uint32_t>(header[0]) << 24) |
                            (static_cast<uint32_t>(header[1]) << 16) |
                            (static_cast<uint32_t>(header[2]) << 8) |
                            static_cast<uint32_t>(header[3]);
    if (length > kHrtMaxFrameBytes) {
        throw ToolError(ExitCode::Data, "FRAME_TOO_LARGE", "hrt frame exceeds 24 MiB");
    }
    std::string payload(length, '\0');
    read = 0;
    while (read < payload.size()) {
        const int got = SSL_read(impl_->ssl, &payload[read],
                                 static_cast<int>(payload.size() - read));
        if (got <= 0) {
            throw NetworkFailure("HRT_IO", "cannot read the hrt frame",
                                 FailureKind::TunnelInterrupted);
        }
        read += static_cast<size_t>(got);
    }
    std::string frame_bytes(reinterpret_cast<const char *>(header), sizeof(header));
    frame_bytes += payload;
    nlohmann::json response = decode_hrt_frame(frame_bytes);
    if (!response.contains("id") || response.at("id") != id) {
        throw ToolError(ExitCode::Data, "BAD_FRAME", "hrt response id does not match");
    }
    return response;
#endif
}

nlohmann::json HrtClient::request(const std::string &method, const std::string &path,
                                  const std::map<std::string, std::string> &headers,
                                  const std::string &body) {
    if (impl_->options.client_cert_pem.empty() ||
        impl_->options.client_key_pem.empty()) {
        throw ToolError(ExitCode::Vault, "HRT_IDENTITY_MISSING",
                        "the vault has no device identity; run hrt-tool login");
    }
    const std::string canonical =
        hrt_request_signing_string(method, path, body, headers);
    nlohmann::json frame{{"op", "request"},
                         {"method", method},
                         {"path", path},
                         {"headers", headers},
                         {"body_base64", base64_encode(body)},
                         {"device_certificate", impl_->options.client_cert_pem},
                         {"device_signature",
                          hrt_sign_request(impl_->options.client_key_pem, canonical)}};
    const nlohmann::json response = call(frame);
    if (response.contains("error") && response.at("error").is_object()) {
        const nlohmann::json &error = response.at("error");
        throw ToolError(ExitCode::Network,
                        error.value("code", std::string("HRT_ERROR")),
                        error.value("message", std::string("hrt request failed")));
    }
    return response;
}
