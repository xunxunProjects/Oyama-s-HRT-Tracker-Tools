#include "error.h"
#include "http.h"
#include "test_framework.h"

#include <string>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <mutex>
#include <thread>

namespace {

/* Minimal SOCKS5 stub: it records the CONNECT target, answers success, then
   closes. A socks5h client must send a domain name here; if it resolved the
   name locally first, the unresolvable .invalid target would fail before the
   proxy saw anything. */
class Socks5Stub {
public:
    Socks5Stub() {
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

    ~Socks5Stub() {
        stopping_ = true;
        if (socket_ >= 0) {
            ::shutdown(socket_, SHUT_RDWR);
            ::close(socket_);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    Socks5Stub(const Socks5Stub &) = delete;
    Socks5Stub &operator=(const Socks5Stub &) = delete;

    bool ok() const { return socket_ >= 0; }
    unsigned short port() const { return port_; }
    std::string target() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return target_;
    }

private:
    static bool read_exact(int fd, unsigned char *buffer, size_t size) {
        size_t offset = 0;
        while (offset < size) {
            const ssize_t got = ::recv(fd, buffer + offset, size - offset, 0);
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
                if (stopping_) {
                    break;
                }
                continue;
            }
            unsigned char greeting[2] = {};
            if (!read_exact(client, greeting, 2) || greeting[0] != 0x05) {
                ::close(client);
                continue;
            }
            std::string methods(greeting[1], '\0');
            if (greeting[1] > 0 &&
                !read_exact(client, reinterpret_cast<unsigned char *>(&methods[0]),
                            methods.size())) {
                ::close(client);
                continue;
            }
            const unsigned char no_auth[2] = {0x05, 0x00};
            ::send(client, no_auth, sizeof(no_auth), MSG_NOSIGNAL);

            unsigned char header[4] = {};
            if (!read_exact(client, header, sizeof(header)) || header[0] != 0x05 ||
                header[1] != 0x01) {
                ::close(client);
                continue;
            }
            std::string host;
            if (header[3] == 0x03) {
                unsigned char length = 0;
                if (!read_exact(client, &length, 1)) {
                    ::close(client);
                    continue;
                }
                host.assign(length, '\0');
                if (length > 0 &&
                    !read_exact(client,
                                reinterpret_cast<unsigned char *>(&host[0]),
                                host.size())) {
                    ::close(client);
                    continue;
                }
            } else if (header[3] == 0x01) {
                unsigned char address[4] = {};
                if (!read_exact(client, address, sizeof(address))) {
                    ::close(client);
                    continue;
                }
                char text[INET_ADDRSTRLEN] = {};
                ::inet_ntop(AF_INET, address, text, sizeof(text));
                host = text;
            } else {
                ::close(client);
                continue;
            }
            unsigned char port[2] = {};
            if (!read_exact(client, port, sizeof(port))) {
                ::close(client);
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                target_ = host;
            }
            const unsigned char success[10] = {0x05, 0x00, 0x00, 0x01,
                                               0,    0,    0,    0,
                                               0,    0};
            ::send(client, success, sizeof(success), MSG_NOSIGNAL);
            ::close(client);
        }
    }

    int socket_ = -1;
    unsigned short port_ = 0;
    std::atomic<bool> stopping_{false};
    mutable std::mutex mutex_;
    std::string target_;
    std::thread thread_;
};

} // namespace
#endif

int main() {
    {
        const Url url = parse_url("https://hrt.mahiro.uk");
        CHECK_EQ(url.scheme, std::string("https"));
        CHECK_EQ(url.host, std::string("hrt.mahiro.uk"));
        CHECK_EQ(url.port, std::string("443"));
        CHECK_EQ(url.path, std::string("/"));
    }

    {
        const Url url = parse_url("https://example.com:8443/api?x=1");
        CHECK_EQ(url.scheme, std::string("https"));
        CHECK_EQ(url.host, std::string("example.com"));
        CHECK_EQ(url.port, std::string("8443"));
        CHECK_EQ(url.path, std::string("/api?x=1"));
    }

    {
        const Url url = parse_url("http://[::1]:8080/");
        CHECK_EQ(url.scheme, std::string("http"));
        CHECK_EQ(url.host, std::string("::1"));
        CHECK_EQ(url.port, std::string("8080"));
    }

    CHECK_THROWS(parse_url("not a url"), ToolError);
    CHECK_THROWS(parse_url("https://"), ToolError);
    CHECK_THROWS(parse_url("https://example.com:99999/"), ToolError);
    CHECK_THROWS(parse_url("https://user@example.com/"), ToolError);

    validate_server_url(parse_url("https://example.com"));
    validate_server_url(parse_url("http://127.0.0.1:8787"));
    validate_server_url(parse_url("http://127.1.2.3:8787"));
    validate_server_url(parse_url("http://localhost:8787"));
    validate_server_url(parse_url("http://abc.onion"));
    validate_server_url(parse_url("http://abc.i2p"));
    CHECK_THROWS(validate_server_url(parse_url("http://example.com")), ToolError);
    CHECK_THROWS(validate_server_url(parse_url("http://127.evil.com")), ToolError);
    CHECK_THROWS(validate_server_url(parse_url("http://127.0.0.1.nip.io")), ToolError);
    CHECK_THROWS(validate_server_url(parse_url("ftp://example.com")), ToolError);

    validate_proxy_url("socks5h://127.0.0.1:9050");
    validate_proxy_url("socks5h://proxy.example:1080");
    /* Only socks5h guarantees that the proxy, not the local resolver, resolves
       the destination name. */
    CHECK_THROWS(validate_proxy_url("socks5://127.0.0.1:4447"), ToolError);
    CHECK_THROWS(validate_proxy_url("http://127.0.0.1:8080"), ToolError);
    CHECK_THROWS(validate_proxy_url("https://proxy.example:8443"), ToolError);
    CHECK_THROWS(validate_proxy_url("ftp://127.0.0.1:21"), ToolError);
    CHECK_THROWS(validate_proxy_url("socks5h://"), ToolError);

    {
        const std::string onion = std::string(56, 'a') + ".onion";
        CHECK(is_onion_host(onion));
        CHECK(is_onion_host(std::string(56, 'a') + ".ONION"));
        CHECK(!is_onion_host("example.com"));
        CHECK(valid_onion_v3(onion));
        CHECK(valid_onion_v3(std::string(56, 'A') + ".onion"));
        CHECK(!valid_onion_v3(std::string(55, 'a') + ".onion"));
        CHECK(!valid_onion_v3(std::string(57, 'a') + ".onion"));
        CHECK(!valid_onion_v3(std::string(56, '1') + ".onion"));
        CHECK(!valid_onion_v3(std::string(56, 'a') + ".i2p"));

        const std::string i2p = "sub.example.i2p";
        CHECK(is_i2p_host(i2p));
        CHECK(valid_i2p_host(i2p));
        CHECK(!valid_i2p_host(std::string(56, 'a') + ".onion"));
        CHECK(!valid_i2p_host(".i2p"));
        CHECK(!valid_i2p_host("bad..i2p"));

        const std::string b32 = std::string(52, 'a') + ".b32.i2p";
        CHECK(is_i2p_host(b32));
        CHECK(is_b32_i2p_host(b32));
        CHECK(valid_i2p_host(b32));
        CHECK(valid_i2p_host(std::string(52, 'A') + ".b32.i2p"));
        CHECK(!valid_i2p_host(std::string(51, 'a') + ".b32.i2p"));
        CHECK(!valid_i2p_host(std::string(53, 'a') + ".b32.i2p"));
        CHECK(!valid_i2p_host(std::string(52, '1') + ".b32.i2p"));
        CHECK(!is_b32_i2p_host(i2p));

        CHECK_EQ(suggested_proxy_for_host(onion),
                 std::string("socks5h://127.0.0.1:9050"));
        CHECK_EQ(suggested_proxy_for_host(i2p),
                 std::string("socks5h://127.0.0.1:4447"));
        CHECK_EQ(suggested_proxy_for_host("example.com"), std::string());
    }

    {
        const std::string redacted = redact_header_line("Authorization: Bearer abc");
        CHECK(redacted.find("abc") == std::string::npos);
        CHECK(redacted.find("redacted") != std::string::npos);
        CHECK_EQ(redact_header_line("Content-Type: application/json"),
                 std::string("Content-Type: application/json"));

        const std::string json =
            redact_json_secrets("{\"token\":\"abc\",\"password\":\"def\",\"x\":\"y\"}");
        CHECK(json.find("abc") == std::string::npos);
        CHECK(json.find("def") == std::string::npos);
        CHECK(json.find("\"y\"") != std::string::npos);
    }

    {
        HttpClient client;
        HttpOptions options;
        CHECK_THROWS(client.request("GET", "http://example.com/", {}, "", options), ToolError);
    }

#ifndef _WIN32
    {
        Socks5Stub proxy;
        CHECK(proxy.ok());
        if (proxy.ok()) {
            HttpOptions options;
            options.proxy = "socks5h://127.0.0.1:" +
                            std::to_string(proxy.port());
            options.timeout_s = 3;
            bool failed = false;
            try {
                HttpClient client;
                client.request("GET", "https://name-that-cannot-resolve.invalid/",
                               {}, "", options);
            } catch (const ToolError &) {
                failed = true;
            }
            CHECK(failed);
            /* The proxy must receive the name, not an IP: if the client
               resolved it locally first, this is empty and the request never
               reaches the proxy. */
            CHECK_EQ(proxy.target(),
                     std::string("name-that-cannot-resolve.invalid"));
        }
    }
#endif

    return test_result();
}
