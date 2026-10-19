#include "http.h"

#include "error.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>

#include <curl/curl.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace {

constexpr size_t kMaxResponseBytes = 16 * 1024 * 1024;

std::string lower_ascii(std::string value) {
    for (char &c : value) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return value;
}

std::string trim(const std::string &value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool ends_with(const std::string &value, const std::string &suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool is_loopback(const std::string &host) {
    if (host == "localhost" || host == "::1") {
        return true;
    }
    in_addr address{};
    if (::inet_pton(AF_INET, host.c_str(), &address) != 1) {
        return false;
    }
    return (ntohl(address.s_addr) >> 24) == 127;
}

std::string hex_encode(const unsigned char *data, size_t len) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0f]);
    }
    return out;
}

void ensure_curl_global() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct WriteContext {
    std::string *body = nullptr;
    bool too_large = false;
};

struct TraceContext {
    std::ostream *out = nullptr;
};

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *context = static_cast<WriteContext *>(userdata);
    const size_t total = size * nmemb;
    if (context->body->size() > kMaxResponseBytes - total) {
        context->too_large = true;
        return 0;
    }
    context->body->append(ptr, total);
    return total;
}

size_t header_callback(char *buffer, size_t size, size_t nitems, void *userdata) {
    auto *headers =
        static_cast<std::multimap<std::string, std::string> *>(userdata);
    const size_t total = size * nitems;
    std::string line(buffer, total);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }
    const std::string::size_type colon = line.find(':');
    if (colon != std::string::npos) {
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
            value.erase(value.begin());
        }
        headers->emplace(std::move(name), std::move(value));
    }
    return total;
}

int debug_callback(CURL *, curl_infotype type, char *data, size_t size, void *userdata) {
    auto *context = static_cast<TraceContext *>(userdata);
    if (context->out == nullptr) {
        return 0;
    }
    if (type == CURLINFO_DATA_IN || type == CURLINFO_DATA_OUT ||
        type == CURLINFO_SSL_DATA_IN || type == CURLINFO_SSL_DATA_OUT) {
        return 0;
    }

    const std::string text(data, size);
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        *context->out << redact_header_line(line) << '\n';
    }
    return 0;
}

std::string spki_sha256(SSL *ssl) {
    X509 *certificate = SSL_get_peer_certificate(ssl);
    if (certificate == nullptr) {
        return {};
    }
    std::string result;
    X509_PUBKEY *public_key = X509_get_X509_PUBKEY(certificate);
    if (public_key != nullptr) {
        unsigned char *der = nullptr;
        const int der_len = i2d_X509_PUBKEY(public_key, &der);
        if (der_len > 0 && der != nullptr) {
            unsigned char digest[EVP_MAX_MD_SIZE];
            unsigned int digest_len = 0;
            if (EVP_Digest(der, static_cast<size_t>(der_len), digest, &digest_len,
                           EVP_sha256(), nullptr) == 1) {
                result = hex_encode(digest, digest_len);
            }
        }
        OPENSSL_free(der);
    }
    X509_free(certificate);
    return result;
}

struct CurlCleanup {
    CURL *curl = nullptr;
    curl_slist *headers = nullptr;
    ~CurlCleanup() {
        if (headers != nullptr) {
            curl_slist_free_all(headers);
        }
        if (curl != nullptr) {
            curl_easy_cleanup(curl);
        }
    }
};

} // namespace

Url parse_url(const std::string &text) {
    const std::string::size_type scheme_end = text.find("://");
    if (scheme_end == std::string::npos || scheme_end == 0) {
        throw ToolError(ExitCode::Usage, "URL", "URL must include a scheme");
    }

    Url url;
    url.scheme = lower_ascii(text.substr(0, scheme_end));
    const std::string rest = text.substr(scheme_end + 3);
    if (rest.empty()) {
        throw ToolError(ExitCode::Usage, "URL", "URL has no host");
    }

    const std::string::size_type authority_end = rest.find_first_of("/?#");
    const std::string authority = rest.substr(0, authority_end);
    if (authority.empty()) {
        throw ToolError(ExitCode::Usage, "URL", "URL has no host");
    }
    if (authority.find('@') != std::string::npos) {
        throw ToolError(ExitCode::Usage, "URL", "URL userinfo is not supported");
    }

    std::string host;
    std::string port;
    if (authority.front() == '[') {
        const std::string::size_type close = authority.find(']');
        if (close == std::string::npos) {
            throw ToolError(ExitCode::Usage, "URL", "invalid IPv6 host");
        }
        host = authority.substr(1, close - 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') {
                throw ToolError(ExitCode::Usage, "URL", "invalid authority");
            }
            port = authority.substr(close + 2);
        }
    } else {
        const std::string::size_type colon = authority.rfind(':');
        if (colon != std::string::npos) {
            if (authority.find(':') != colon) {
                throw ToolError(ExitCode::Usage, "URL", "invalid authority");
            }
            host = authority.substr(0, colon);
            port = authority.substr(colon + 1);
        } else {
            host = authority;
        }
    }

    if (host.empty()) {
        throw ToolError(ExitCode::Usage, "URL", "URL has no host");
    }
    url.host = lower_ascii(host);

    if (port.empty()) {
        if (url.scheme == "https") {
            port = "443";
        } else if (url.scheme == "http") {
            port = "80";
        } else if (url.scheme == "socks5" || url.scheme == "socks5h") {
            port = "1080";
        }
    }
    if (!port.empty()) {
        if (!std::all_of(port.begin(), port.end(),
                         [](char c) { return c >= '0' && c <= '9'; })) {
            throw ToolError(ExitCode::Usage, "URL", "URL port is not numeric");
        }
        int value = 0;
        try {
            value = std::stoi(port);
        } catch (...) {
            throw ToolError(ExitCode::Usage, "URL", "URL port is out of range");
        }
        if (value < 1 || value > 65535) {
            throw ToolError(ExitCode::Usage, "URL", "URL port is out of range");
        }
    }
    url.port = port;

    if (authority_end == std::string::npos) {
        url.path = "/";
    } else if (rest[authority_end] == '/') {
        url.path = rest.substr(authority_end);
    } else {
        url.path = "/" + rest.substr(authority_end);
    }
    return url;
}

void validate_server_url(const Url &url) {
    if (url.scheme == "https") {
        return;
    }
    if (url.scheme != "http") {
        throw ToolError(ExitCode::Usage, "SERVER_URL",
                        "server URL must use https:// (or http:// for loopback, .onion, or .i2p)");
    }
    if (is_loopback(url.host) || ends_with(url.host, ".onion") ||
        ends_with(url.host, ".i2p")) {
        return;
    }
    throw ToolError(ExitCode::Usage, "SERVER_URL",
                    "http:// is only allowed for loopback, .onion, or .i2p servers");
}

void validate_proxy_url(const std::string &proxy) {
    const Url url = parse_url(proxy);
    if (url.scheme != "socks5" && url.scheme != "socks5h" && url.scheme != "http" &&
        url.scheme != "https") {
        throw ToolError(ExitCode::Usage, "PROXY_URL",
                        "proxy must use socks5h://, socks5://, http://, or https://");
    }
    if (url.host.empty()) {
        throw ToolError(ExitCode::Usage, "PROXY_URL", "proxy URL has no host");
    }
}

std::string redact_header_line(const std::string &header_line) {
    const std::string::size_type colon = header_line.find(':');
    if (colon == std::string::npos) {
        return header_line;
    }
    const std::string name = lower_ascii(trim(header_line.substr(0, colon)));
    if (name == "authorization" || name == "proxy-authorization" || name == "cookie" ||
        name == "set-cookie") {
        return header_line.substr(0, colon) + ": <redacted>";
    }
    return header_line;
}

std::string redact_json_secrets(const std::string &json) {
    static const char *const keys[] = {"token",     "password",      "device_pass",
                                       "cloud_key", "transport_key", "vault_key"};
    std::string result = json;
    for (const char *key : keys) {
        const std::string needle = std::string("\"") + key + "\"";
        std::string::size_type search_from = 0;
        while (true) {
            const std::string::size_type key_pos = result.find(needle, search_from);
            if (key_pos == std::string::npos) {
                break;
            }
            const std::string::size_type colon = result.find(':', key_pos + needle.size());
            if (colon == std::string::npos) {
                break;
            }
            const std::string::size_type value_start = result.find('"', colon + 1);
            if (value_start == std::string::npos) {
                break;
            }
            std::string::size_type value_end = value_start + 1;
            while (value_end < result.size()) {
                if (result[value_end] == '\\') {
                    value_end += 2;
                    continue;
                }
                if (result[value_end] == '"') {
                    break;
                }
                ++value_end;
            }
            if (value_end >= result.size()) {
                break;
            }
            result.replace(value_start + 1, value_end - value_start - 1, "<redacted>");
            search_from = value_start + 11;
        }
    }
    return result;
}

HttpResponse HttpClient::request(const std::string &method, const std::string &url,
                                 const std::vector<std::string> &headers,
                                 const std::string &body, const HttpOptions &options) {
    const Url parsed = parse_url(url);
    validate_server_url(parsed);
    if (!options.proxy.empty()) {
        validate_proxy_url(options.proxy);
    }
    if (body.size() > static_cast<size_t>(LONG_MAX)) {
        throw ToolError(ExitCode::Network, "HTTP_TOO_LARGE", "request body is too large");
    }

    ensure_curl_global();

    CurlCleanup cleanup;
    cleanup.curl = curl_easy_init();
    if (cleanup.curl == nullptr) {
        throw ToolError(ExitCode::Network, "HTTP", "cannot initialize libcurl");
    }

    HttpResponse response;
    WriteContext write_context{&response.body, false};
    TraceContext trace_context{options.trace_out};

    for (const std::string &header : headers) {
        cleanup.headers = curl_slist_append(cleanup.headers, header.c_str());
    }

    CURL *curl = cleanup.curl;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "hrt-tool/0.1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, options.timeout_s);
    // Never follow a redirect automatically: a 307/308 would re-send the
    // login body (the account password) to whatever host the response names.
    // M4 may add a loop that re-validates the URL policy and trust pin on every
    // hop; until then a 3xx is a typed error.
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, options.insecure_tls ? 0L : 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, options.insecure_tls ? 0L : 2L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_context);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
    if (!headers.empty()) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, cleanup.headers);
    }

    if (method == "GET") {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    if (!options.proxy.empty()) {
        const Url proxy = parse_url(options.proxy);
        curl_easy_setopt(curl, CURLOPT_PROXY, options.proxy.c_str());
        if (proxy.scheme == "socks5h") {
            curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);
        } else if (proxy.scheme == "socks5") {
            curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5);
        } else if (proxy.scheme == "https") {
            curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTPS);
        } else {
            curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
        }
    }

    if (options.trace) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, debug_callback);
        curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &trace_context);
    }

    const CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        if (write_context.too_large) {
            throw ToolError(ExitCode::Network, "HTTP_TOO_LARGE",
                            "server response is larger than 16 MiB");
        }
        throw ToolError(ExitCode::Network, "HTTP", curl_easy_strerror(result));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);

    struct curl_tlssessioninfo *tls = nullptr;
    if (curl_easy_getinfo(curl, CURLINFO_TLS_SSL_PTR, &tls) == CURLE_OK && tls != nullptr &&
        tls->internals != nullptr && tls->backend == CURLSSLBACKEND_OPENSSL) {
        response.spki_sha256 = spki_sha256(static_cast<SSL *>(tls->internals));
    }

    if (options.debug && options.trace_out != nullptr) {
        *options.trace_out << method << ' ' << url << " -> " << response.status << '\n';
        if (!response.spki_sha256.empty()) {
            *options.trace_out << "SPKI-SHA256: " << response.spki_sha256 << '\n';
        }
    }

    return response;
}
