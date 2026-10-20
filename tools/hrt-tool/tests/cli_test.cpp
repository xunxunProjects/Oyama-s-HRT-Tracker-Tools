#include "cli.h"
#include "test_framework.h"
#include "vault.h"

#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#endif

namespace {

int g_temp_counter = 0;

class TempDir {
public:
    TempDir() {
        const char *base = std::getenv("TMPDIR");
        const std::filesystem::path root =
            base != nullptr && *base != '\0' ? std::filesystem::path(base)
                                             : std::filesystem::temp_directory_path();
        while (true) {
            path_ = root / ("hrt-tool-cli-test-" + std::to_string(++g_temp_counter));
            if (std::filesystem::create_directories(path_)) {
                break;
            }
        }
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;
    const std::filesystem::path &path() const { return path_; }

private:
    std::filesystem::path path_;
};

struct RunResult {
    int code;
    std::string out;
    std::string err;
};

RunResult run(const std::vector<std::string> &args, const std::string &input = "") {
    std::vector<const char *> argv;
    argv.push_back("hrt-tool");
    for (const std::string &arg : args) {
        argv.push_back(arg.c_str());
    }

    std::istringstream in(input);
    std::ostringstream out;
    std::ostringstream err;
    const int code = run_cli(static_cast<int>(argv.size()), argv.data(), in, out, err);
    return RunResult{code, out.str(), err.str()};
}

bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string read_text(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

const char *kPasswordLine = "correct horse battery staple\n";

int64_t test_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

#ifndef _WIN32
struct TestCertificate {
    std::string cert_pem;
    std::string key_pem;
};

TestCertificate make_test_certificate(const char *common_name, long serial) {
    TestCertificate result;
    EVP_PKEY_CTX *key_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    EVP_PKEY *key = nullptr;
    const bool key_ok =
        key_ctx != nullptr && EVP_PKEY_keygen_init(key_ctx) == 1 &&
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(key_ctx,
                                               NID_X9_62_prime256v1) == 1 &&
        EVP_PKEY_keygen(key_ctx, &key) == 1;
    if (!key_ok) {
        EVP_PKEY_CTX_free(key_ctx);
        return result;
    }
    X509 *cert = X509_new();
    if (cert != nullptr) {
        X509_set_version(cert, 2);
        ASN1_INTEGER_set(X509_get_serialNumber(cert), serial);
        X509_gmtime_adj(X509_getm_notBefore(cert), -60);
        X509_gmtime_adj(X509_getm_notAfter(cert), 86400);
        X509_set_pubkey(cert, key);
        X509_NAME *name = X509_get_subject_name(cert);
        X509_NAME_add_entry_by_txt(
            name, "CN", MBSTRING_ASC,
            reinterpret_cast<const unsigned char *>(common_name), -1, -1, 0);
        X509_set_issuer_name(cert, name);
        if (X509_sign(cert, key, EVP_sha256()) > 0) {
            BIO *bio = BIO_new(BIO_s_mem());
            if (bio != nullptr) {
                PEM_write_bio_X509(bio, cert);
                char *data = nullptr;
                const long length = BIO_get_mem_data(bio, &data);
                result.cert_pem.assign(data, static_cast<size_t>(length));
                BIO_free(bio);
            }
            bio = BIO_new(BIO_s_mem());
            if (bio != nullptr) {
                PEM_write_bio_PrivateKey(bio, key, nullptr, nullptr, 0, nullptr,
                                         nullptr);
                char *data = nullptr;
                const long length = BIO_get_mem_data(bio, &data);
                result.key_pem.assign(data, static_cast<size_t>(length));
                BIO_free(bio);
            }
        }
        X509_free(cert);
    }
    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(key_ctx);
    return result;
}

SSL_CTX *make_server_context(const TestCertificate &certificate) {
    if (certificate.cert_pem.empty() || certificate.key_pem.empty()) {
        return nullptr;
    }
    SSL_CTX *context = SSL_CTX_new(TLS_server_method());
    if (context == nullptr) {
        return nullptr;
    }
    BIO *cert_bio = BIO_new_mem_buf(certificate.cert_pem.data(),
                                    static_cast<int>(certificate.cert_pem.size()));
    X509 *cert = cert_bio == nullptr
                     ? nullptr
                     : PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
    BIO_free(cert_bio);
    BIO *key_bio = BIO_new_mem_buf(certificate.key_pem.data(),
                                   static_cast<int>(certificate.key_pem.size()));
    EVP_PKEY *key = key_bio == nullptr
                        ? nullptr
                        : PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
    BIO_free(key_bio);
    const bool ok = cert != nullptr && key != nullptr &&
                    SSL_CTX_use_certificate(context, cert) == 1 &&
                    SSL_CTX_use_PrivateKey(context, key) == 1;
    X509_free(cert);
    EVP_PKEY_free(key);
    if (!ok) {
        SSL_CTX_free(context);
        return nullptr;
    }
    return context;
}

/* Loopback HTTP(S) stub for the live trust flow; POSIX only, like the CLI
   tests that need real sockets. */
class LoopbackServer {
public:
    explicit LoopbackServer(const TestCertificate *tls = nullptr) {
        if (tls != nullptr) {
            context_ = make_server_context(*tls);
            tls_enabled_ = context_ != nullptr;
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
        ::listen(socket_, 8);
        socklen_t length = sizeof(address);
        ::getsockname(socket_, reinterpret_cast<sockaddr *>(&address), &length);
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this] { serve(); });
    }

    ~LoopbackServer() {
        stopping_ = true;
        if (socket_ >= 0) {
            ::shutdown(socket_, SHUT_RDWR);
            ::close(socket_);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        SSL_CTX_free(context_);
    }

    LoopbackServer(const LoopbackServer &) = delete;
    LoopbackServer &operator=(const LoopbackServer &) = delete;

    bool ok() const { return socket_ >= 0; }
    bool tls() const { return tls_enabled_; }
    std::string url() const {
        return std::string(tls_enabled_ ? "https" : "http") +
               "://127.0.0.1:" + std::to_string(port_);
    }

    /* Rotate the certificate after serving this path once, so the next
       connection in the same command sees a different key. */
    void rotate_after(const std::string &path,
                      const TestCertificate &certificate) {
        std::lock_guard<std::mutex> lock(mutex_);
        rotate_path_ = path;
        rotate_certificate_ = certificate;
    }

    void fail_login(bool fail) { fail_login_ = fail; }
    void drop_login(bool drop) { drop_login_ = drop; }

    int authenticated_requests() const { return authenticated_requests_; }
    int snapshot_puts() const { return snapshot_puts_; }
    std::string last_snapshot_body() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_snapshot_body_;
    }

private:
    void serve() {
        while (!stopping_) {
            const int client = ::accept(socket_, nullptr, nullptr);
            if (client < 0) {
                if (stopping_) {
                    break;
                }
                continue;
            }
            SSL_CTX *context = nullptr;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                context = context_;
                if (context != nullptr) {
                    SSL_CTX_up_ref(context);
                }
            }
            if (context != nullptr) {
                SSL *ssl = SSL_new(context);
                SSL_CTX_free(context);
                if (ssl != nullptr) {
                    SSL_set_fd(ssl, client);
                    if (SSL_accept(ssl) == 1) {
                        respond(client, ssl);
                        SSL_shutdown(ssl);
                    }
                    SSL_free(ssl);
                }
            } else {
                respond(client, nullptr);
            }
            ::close(client);
        }
    }

    static ssize_t read_some(SSL *ssl, int client, char *buffer, size_t size) {
        return ssl != nullptr ? SSL_read(ssl, buffer, static_cast<int>(size))
                              : ::recv(client, buffer, size, 0);
    }

    static void write_all(SSL *ssl, int client, const std::string &data) {
        size_t offset = 0;
        while (offset < data.size()) {
            const int written =
                ssl != nullptr
                    ? SSL_write(ssl, data.data() + offset,
                                static_cast<int>(data.size() - offset))
                    : static_cast<int>(::send(client, data.data() + offset,
                                              data.size() - offset,
                                              MSG_NOSIGNAL));
            if (written <= 0) {
                return;
            }
            offset += static_cast<size_t>(written);
        }
    }

    void respond(int client, SSL *ssl) {
        std::string request;
        char buffer[4096];
        bool headers_done = false;
        size_t header_end = 0;
        size_t content_length = 0;
        while (request.size() < 65536) {
            const ssize_t got = read_some(ssl, client, buffer, sizeof(buffer));
            if (got <= 0) {
                break;
            }
            request.append(buffer, static_cast<size_t>(got));
            if (!headers_done) {
                const size_t split = request.find("\r\n\r\n");
                if (split != std::string::npos) {
                    headers_done = true;
                    header_end = split + 4;
                    std::string lower = request.substr(0, split);
                    for (char &c : lower) {
                        c = static_cast<char>(std::tolower(
                            static_cast<unsigned char>(c)));
                    }
                    const std::string needle = "\r\ncontent-length:";
                    const size_t at = lower.find(needle);
                    if (at != std::string::npos) {
                        content_length = static_cast<size_t>(
                            std::strtoul(request.c_str() + at + needle.size(),
                                         nullptr, 10));
                    }
                }
            }
            if (headers_done && request.size() >= header_end + content_length) {
                break;
            }
        }

        std::string lower = request;
        for (char &c : lower) {
            c = static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
        }
        if (lower.find("\r\nauthorization: bearer") != std::string::npos) {
            ++authenticated_requests_;
        }

        const size_t first_space = request.find(' ');
        const size_t second_space =
            first_space == std::string::npos
                ? std::string::npos
                : request.find(' ', first_space + 1);
        const std::string method =
            first_space == std::string::npos ? std::string{}
                                             : request.substr(0, first_space);
        const std::string path =
            first_space == std::string::npos || second_space == std::string::npos
                ? std::string{}
                : request.substr(first_space + 1, second_space - first_space - 1);

        if (path == "/api/tool/v1/login" && drop_login_) {
            return;
        }

        std::string status = "200 OK";
        std::string body;
        if (path == "/api/tool/v1/version") {
            body = R"({"apiVersion":1,"minToolVersion":"0.1.0","transport":["tls"],)"
                   R"("serverTime":"2026-10-20T02:00:00.000Z"})";
        } else if (path == "/api/tool/v1/login") {
            if (fail_login_) {
                status = "401 Unauthorized";
                body = R"({"code":"INVALID_CREDENTIALS","message":"bad credentials"})";
            } else {
                body = R"({"token":"tok-test-1","user":{"id":"user-1",)"
                       R"("username":"Alice","isAdmin":false},)"
                       R"("devicePass":"dev-pass-1","apiVersion":1})";
            }
        } else if (path == "/api/tool/v1/me") {
            body = R"({"user":{"id":"user-1","username":"Alice","isAdmin":false},)"
                   R"("serverTime":"2026-10-20T02:00:00.000Z","apiVersion":1})";
        } else if (path == "/api/tool/v1/logout") {
            body = R"({"ok":true})";
        } else if (path == "/api/tool/v1/snapshot") {
            if (method == "PUT") {
                ++snapshot_puts_;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    last_snapshot_body_ =
                        headers_done ? request.substr(header_end) : std::string{};
                }
                body = R"({"id":"00000000-0000-4000-8000-000000000002",)"
                       R"("createdAt":1792495000})";
            } else {
                status = "404 Not Found";
                body = R"({"code":"NO_SNAPSHOT","message":"none"})";
            }
        } else {
            status = "404 Not Found";
            body = R"({"code":"NOT_FOUND","message":"no"})";
        }

        const std::string response =
            "HTTP/1.1 " + status +
            "\r\nContent-Type: application/json\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        write_all(ssl, client, response);

        std::lock_guard<std::mutex> lock(mutex_);
        if (!rotate_path_.empty() && path == rotate_path_) {
            SSL_CTX *next = make_server_context(rotate_certificate_);
            if (next != nullptr) {
                SSL_CTX_free(context_);
                context_ = next;
            }
            rotate_path_.clear();
        }
    }

    int socket_ = -1;
    unsigned short port_ = 0;
    bool tls_enabled_ = false;
    SSL_CTX *context_ = nullptr;
    std::string rotate_path_;
    TestCertificate rotate_certificate_;
    std::atomic<bool> fail_login_{false};
    std::atomic<bool> drop_login_{false};
    std::atomic<int> authenticated_requests_{0};
    std::atomic<int> snapshot_puts_{0};
    std::string last_snapshot_body_;
    std::atomic<bool> stopping_{false};
    mutable std::mutex mutex_;
    std::thread thread_;
};
#endif

} // namespace

int main() {
#ifndef _WIN32
    /* A client that aborts a pinned TLS handshake can make the stub's
       SSL_write hit a closed socket. */
    std::signal(SIGPIPE, SIG_IGN);
#endif
    {
        const RunResult result = run({"version"});
        CHECK_EQ(result.code, 0);
        CHECK(contains(result.out, "hrt-tool 0.1.0"));
        CHECK(contains(result.out, "hrtcore 0.1.0"));
        CHECK(contains(result.out, "hrtprint 0.1.0"));
    }

    {
        const RunResult result = run({"--help"});
        CHECK_EQ(result.code, 0);
        for (const char *word : {"init", "login", "look", "add", "export", "import",
                                 "snapshot", "config", "logs", "version"}) {
            CHECK(contains(result.out, word));
        }
    }

    {
        const RunResult result = run({"bogus"});
        CHECK_EQ(result.code, 2);
    }

    {
        TempDir temp;
        const RunResult result = run({"config", "path", "--config-dir", temp.path().string()});
        CHECK_EQ(result.code, 0);
        CHECK(contains(result.out, temp.path().string()));
        CHECK(contains(result.out, "vault.hrtv"));
        CHECK(!std::filesystem::exists(temp.path() / "vault.hrtv"));
    }

    {
        TempDir temp;
        const RunResult result = run(
            {"init", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(result.code, 0);
        CHECK(std::filesystem::exists(temp.path() / "vault.hrtv"));
        CHECK(contains(result.out, "guess"));
        CHECK(contains(result.out, "uniform"));
        CHECK(contains(result.out, "classes:"));
    }

    {
        TempDir temp;
        const RunResult result = run(
            {"init", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            "aaaaaaaaaaaaaaaa\n");
        CHECK_EQ(result.code, 3);
        CHECK(!std::filesystem::exists(temp.path() / "vault.hrtv"));
    }

    {
        TempDir temp;
        std::string long_password(300, 'a');
        long_password.push_back('\n');
        const RunResult result = run(
            {"init", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            long_password);
        CHECK_EQ(result.code, 5);
        CHECK(!std::filesystem::exists(temp.path() / "vault.hrtv"));
    }

    {
        TempDir temp;
        const std::filesystem::path password_file = temp.path() / "long.txt";
        {
            std::ofstream out(password_file);
            out << std::string(300, 'a') << "\n";
        }
#ifndef _WIN32
        ::chmod(password_file.c_str(), 0600);
#endif
        const RunResult result =
            run({"init", "--config-dir", temp.path().string(), "--vault-password-file",
                 password_file.string()});
        CHECK_EQ(result.code, 5);
        CHECK(!std::filesystem::exists(temp.path() / "vault.hrtv"));
    }

    {
        TempDir temp;
        const RunResult result = run(
            {"login", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(result.code, 3);
        CHECK(contains(result.err, "init"));
    }

    {
        TempDir temp;
        const RunResult init = run(
            {"init", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(init.code, 0);

        const auto with_vault = [&](std::vector<std::string> args) {
            args.push_back("--config-dir");
            args.push_back(temp.path().string());
            args.push_back("--vault-password-stdin");
            return args;
        };

        /* Live commands now reach the API; with no credentials/token they
           fail at the usage/auth boundary instead of the M1 stub. */
        CHECK_EQ(run(with_vault({"login"}), kPasswordLine).code, 2);
        const RunResult logout = run(with_vault({"logout"}), kPasswordLine);
        CHECK_EQ(logout.code, 0);
        CHECK(contains(logout.out, "not logged in"));
        CHECK_EQ(run(with_vault({"whoami"}), kPasswordLine).code, 3);
        CHECK_EQ(run(with_vault({"snapshot", "pull", "--out", "-"}), kPasswordLine).code, 3);

        const RunResult config_get = run(with_vault({"config", "get"}), kPasswordLine);
        CHECK_EQ(config_get.code, 0);
        CHECK(contains(config_get.out, "server:"));
        CHECK(contains(config_get.out, "proxy: (none)"));
    }

    {
        TempDir temp;
        const RunResult init = run(
            {"init", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(init.code, 0);

        const RunResult remote_look =
            run({"look", "--config-dir", temp.path().string(),
                 "--vault-password-stdin"},
                kPasswordLine);
        CHECK_EQ(remote_look.code, 3);
        CHECK(contains(remote_look.err, "log in first"));

        /* With a vault but no session, the live mutation commands ask for a
           login; export/import remain offline-only and ask for --snapshot. */
        for (const char *command : {"add", "dose", "lab", "weight"}) {
            std::vector<std::string> args{command};
            args.push_back("--config-dir");
            args.push_back(temp.path().string());
            args.push_back("--vault-password-stdin");
            const RunResult result = run(args, kPasswordLine);
            CHECK_EQ(result.code, 3);
            CHECK(contains(result.err, "log in first"));
        }
        for (const char *command : {"export", "import"}) {
            std::vector<std::string> args{command};
            args.push_back("--config-dir");
            args.push_back(temp.path().string());
            args.push_back("--vault-password-stdin");
            const RunResult result = run(args, kPasswordLine);
            CHECK_EQ(result.code, 2);
            CHECK(contains(result.err, "--snapshot") ||
                  contains(result.err, "missing --out") ||
                  contains(result.err, "missing --in"));
        }

        const std::filesystem::path password_file = temp.path() / "password.txt";
        {
            std::ofstream out(password_file);
            out << kPasswordLine;
        }
#ifndef _WIN32
        ::chmod(password_file.c_str(), 0644);
#endif
        const RunResult result =
            run({"login", "--config-dir", temp.path().string(), "--vault-password-file",
                 password_file.string()});
        CHECK_EQ(result.code, 3);

        const RunResult missing_log =
            run({"logs", "show", "--config-dir", temp.path().string(),
                 "--vault-password-stdin"},
                kPasswordLine);
        CHECK_EQ(missing_log.code, 2);
    }

    {
        TempDir temp;
        const RunResult init = run(
            {"init", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(init.code, 0);

        const RunResult again = run(
            {"init", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(again.code, 3);

        const RunResult refused = run(
            {"init", "--reset", "--config-dir", temp.path().string(),
             "--vault-password-stdin"},
            "no\n" + std::string(kPasswordLine));
        CHECK_EQ(refused.code, 2);
        CHECK(std::filesystem::exists(temp.path() / "vault.hrtv"));

        const RunResult reset = run(
            {"init", "--reset", "--yes", "--config-dir", temp.path().string(),
             "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(reset.code, 0);
    }

    {
        TempDir temp;
        const RunResult init = run(
            {"init", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(init.code, 0);

        const RunResult config = run(
            {"config", "get", "--config-dir", temp.path().string(),
             "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(config.code, 0);
        CHECK(contains(config.out, "server:"));
        CHECK(contains(config.out, "proxy:"));

        const std::filesystem::path log_file = temp.path() / "debug.log";
        const RunResult stub = run(
            {"logs", "bogus", "--config-dir", temp.path().string(),
             "--vault-password-stdin", "--debug", "--log-file", log_file.string()},
            kPasswordLine);
        CHECK_EQ(stub.code, 6);
        CHECK(contains(stub.err, "debug: logs bogus"));
        CHECK(std::filesystem::exists(log_file));

        const std::string raw = read_text(log_file);
        CHECK(!contains(raw, "logs bogus"));
        CHECK(!contains(raw, "not implemented"));

        const RunResult shown = run(
            {"logs", "show", "--config-dir", temp.path().string(), "--vault-password-stdin",
             "--log-file", log_file.string()},
            kPasswordLine);
        CHECK_EQ(shown.code, 0);
        CHECK(contains(shown.out, "logs bogus"));
    }

#ifndef _WIN32
    {
        LoopbackServer server;
        CHECK(server.ok());
        if (server.ok()) {
            TempDir temp;
            const std::filesystem::path account = temp.path() / "account.txt";
            {
                std::ofstream out(account);
                out << "account-password\n";
            }
            ::chmod(account.c_str(), 0600);

            const RunResult init = run(
                {"init", "--config-dir", temp.path().string(),
                 "--vault-password-stdin"},
                kPasswordLine);
            CHECK_EQ(init.code, 0);

            const RunResult login = run(
                {"login", "--server", server.url() + "/", "--username", "Alice",
                 "--account-password-file", account.string(),
                 "--vault-password-stdin", "--config-dir", temp.path().string()},
                std::string(kPasswordLine) + "yes\n");
            CHECK_EQ(login.code, 0);
            CHECK(contains(login.err, "first contact with " + server.url()));
            CHECK(contains(login.err, "plain HTTP"));
            CHECK(contains(login.out, "Alice"));

            /* The interactive yes pins the origin; the next command must not
               prompt again even though no --trust-server was given. Proxy
               environment variables must not be used when no proxy is
               configured. */
            ::setenv("ALL_PROXY", "http://127.0.0.1:1", 1);
            ::setenv("http_proxy", "http://127.0.0.1:1", 1);
            const RunResult whoami = run(
                {"whoami", "--vault-password-stdin",
                 "--config-dir", temp.path().string()},
                kPasswordLine);
            ::unsetenv("ALL_PROXY");
            ::unsetenv("http_proxy");
            CHECK_EQ(whoami.code, 0);
            CHECK(contains(whoami.out, "Alice"));
            CHECK(!contains(whoami.err, "first contact"));

            const RunResult logout = run(
                {"logout", "--vault-password-stdin",
                 "--config-dir", temp.path().string()},
                kPasswordLine);
            CHECK_EQ(logout.code, 0);
            CHECK(contains(logout.out, "logged out"));
        }
    }

    {
        const TestCertificate first = make_test_certificate("hrt-test-one", 1);
        const TestCertificate second = make_test_certificate("hrt-test-two", 2);
        CHECK(!first.cert_pem.empty());
        CHECK(!second.cert_pem.empty());
        LoopbackServer server(&first);
        CHECK(server.ok());
        CHECK(server.tls());
        if (server.ok() && server.tls()) {
            TempDir temp;
            const std::filesystem::path account = temp.path() / "account.txt";
            {
                std::ofstream out(account);
                out << "account-password\n";
            }
            ::chmod(account.c_str(), 0600);
            const RunResult init = run(
                {"init", "--config-dir", temp.path().string(),
                 "--vault-password-stdin"},
                kPasswordLine);
            CHECK_EQ(init.code, 0);

            const RunResult login = run(
                {"login", "--insecure-tls", "--trust-server", "--server",
                 server.url(), "--username", "Alice", "--account-password-file",
                 account.string(), "--vault-password-stdin", "--config-dir",
                 temp.path().string()},
                kPasswordLine);
            CHECK_EQ(login.code, 0);
            CHECK(contains(login.err, "SPKI SHA-256"));

            /* Serve the trust probe with the pinned key, then swap keys before
               the authenticated request. The token must never cross the new
               connection. */
            server.rotate_after("/api/tool/v1/version", second);
            const RunResult whoami = run(
                {"whoami", "--insecure-tls", "--vault-password-stdin",
                 "--config-dir", temp.path().string()},
                kPasswordLine);
            CHECK_EQ(whoami.code, 4);
            CHECK(contains(whoami.err, "TRUST_CHANGED"));
            CHECK_EQ(server.authenticated_requests(), 0);

            const RunResult retrust = run(
                {"whoami", "--insecure-tls", "--trust-server",
                 "--vault-password-stdin", "--config-dir", temp.path().string()},
                kPasswordLine);
            CHECK_EQ(retrust.code, 0);
            CHECK(contains(retrust.out, "Alice"));
            CHECK_EQ(server.authenticated_requests(), 1);
        }
    }

    {
        TempDir temp;
        const RunResult init = run(
            {"init", "--config-dir", temp.path().string(),
             "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(init.code, 0);

        LoopbackServer server;
        CHECK(server.ok());
        if (server.ok()) {
            /* Refusing the first-contact prompt must stop before any
               credential is read or sent. */
            const RunResult refused = run(
                {"login", "--server", server.url(), "--username", "Alice",
                 "--vault-password-stdin", "--config-dir", temp.path().string()},
                std::string(kPasswordLine) + "no\n");
            CHECK_EQ(refused.code, 4);
            CHECK(contains(refused.err, "TRUST_REFUSED"));
        }

        const std::string onion = std::string(56, 'a') + ".onion";
        const RunResult no_proxy = run(
            {"login", "--server", "http://" + onion, "--username", "Alice",
             "--timeout", "3", "--vault-password-stdin",
             "--config-dir", temp.path().string()},
            kPasswordLine);
        CHECK_EQ(no_proxy.code, 4);
        CHECK(contains(no_proxy.err, "TOR_PROXY_MISSING"));
        CHECK(contains(no_proxy.err,
                       "config set proxy socks5h://127.0.0.1:9050"));

        const RunResult bad_onion = run(
            {"config", "set", "server", "http://abc.onion",
             "--vault-password-stdin", "--config-dir", temp.path().string()},
            kPasswordLine);
        CHECK_EQ(bad_onion.code, 2);
        CHECK(contains(bad_onion.err, "ONION_ADDRESS"));

        /* Only socks5h may be configured: socks5 resolves DNS locally. */
        const RunResult set_local_dns = run(
            {"config", "set", "proxy", "socks5://127.0.0.1:9050",
             "--vault-password-stdin", "--config-dir", temp.path().string()},
            kPasswordLine);
        CHECK_EQ(set_local_dns.code, 2);
        CHECK(contains(set_local_dns.err, "PROXY_URL"));
        CHECK(contains(set_local_dns.err, "socks5h"));

        const RunResult override_local_dns = run(
            {"login", "--server", "https://example.com", "--username", "Alice",
             "--proxy", "http://127.0.0.1:8080", "--vault-password-stdin",
             "--config-dir", temp.path().string()},
            kPasswordLine);
        CHECK_EQ(override_local_dns.code, 2);
        CHECK(contains(override_local_dns.err, "PROXY_URL"));

        const RunResult set_proxy = run(
            {"config", "set", "proxy", "socks5h://127.0.0.1:9050",
             "--vault-password-stdin", "--config-dir", temp.path().string()},
            kPasswordLine);
        CHECK_EQ(set_proxy.code, 0);
        CHECK(contains(set_proxy.out, "proxy set"));
    }

    {
        LoopbackServer server;
        CHECK(server.ok());
        server.drop_login(true);
        if (server.ok()) {
            TempDir temp;
            const std::filesystem::path account = temp.path() / "account.txt";
            {
                std::ofstream out(account);
                out << "account-password\n";
            }
            ::chmod(account.c_str(), 0600);
            const RunResult init = run(
                {"init", "--config-dir", temp.path().string(),
                 "--vault-password-stdin"},
                kPasswordLine);
            CHECK_EQ(init.code, 0);

            const std::vector<std::string> args{
                "login", "--server", server.url(), "--username", "Alice",
                "--account-password-file", account.string(), "--trust-server",
                "--vault-password-stdin", "--config-dir", temp.path().string()};
            const std::filesystem::path vault = temp.path() / "vault.hrtv";
            const std::string master = "correct horse battery staple";

            /* A transport failure is not a credential failure: it must not
               start the local pause. */
            const RunResult network_error = run(args, kPasswordLine);
            CHECK_EQ(network_error.code, 4);
            CHECK(!contains(network_error.err, "LOGIN_COOLDOWN"));
            {
                VaultSession session = Vault::unlock(vault, master);
                CHECK_EQ(session.data.last_login_failed_at,
                         static_cast<int64_t>(0));
            }

            server.drop_login(false);
            server.fail_login(true);
            const RunResult first = run(args, kPasswordLine);
            CHECK_EQ(first.code, 3);
            CHECK(contains(first.err, "INVALID_CREDENTIALS"));
            CHECK(!contains(first.err, "LOGIN_COOLDOWN"));

            /* The next invocation is refused locally, without another login
               request, until the 60-second window has passed. */
            const RunResult second = run(args, kPasswordLine);
            CHECK_EQ(second.code, 3);
            CHECK(contains(second.err, "LOGIN_COOLDOWN"));

            {
                VaultSession session = Vault::unlock(vault, master);
                session.data.last_login_failed_at = test_now_ms() - 61000;
                Vault::save(vault, session.data, master);
            }
            const RunResult third = run(args, kPasswordLine);
            CHECK_EQ(third.code, 3);
            CHECK(contains(third.err, "INVALID_CREDENTIALS"));
            CHECK(!contains(third.err, "LOGIN_COOLDOWN"));

            server.fail_login(false);
            {
                VaultSession session = Vault::unlock(vault, master);
                session.data.last_login_failed_at = test_now_ms() - 61000;
                Vault::save(vault, session.data, master);
            }
            const RunResult success = run(args, kPasswordLine);
            CHECK_EQ(success.code, 0);
            const VaultSession session = Vault::unlock(vault, master);
            CHECK_EQ(session.data.last_login_failed_at, static_cast<int64_t>(0));
        }
    }

    {
        /* Live mutation: login, then `add` with no --snapshot pulls the
           (missing) revision, applies the dose, encrypts the payload and
           PUTs it. The mock rejects GET with NO_SNAPSHOT and records the
           write so the test can see the cloud bundle leave the CLI. */
        LoopbackServer server;
        CHECK(server.ok());
        if (server.ok()) {
            TempDir temp;
            const std::filesystem::path account = temp.path() / "account.txt";
            {
                std::ofstream out(account);
                out << "account-password\n";
            }
            ::chmod(account.c_str(), 0600);
            const RunResult init = run(
                {"init", "--config-dir", temp.path().string(),
                 "--vault-password-stdin"},
                kPasswordLine);
            CHECK_EQ(init.code, 0);

            const RunResult login = run(
                {"login", "--trust-server", "--server", server.url(),
                 "--username", "Alice", "--account-password-file",
                 account.string(), "--vault-password-stdin", "--config-dir",
                 temp.path().string()},
                kPasswordLine);
            CHECK_EQ(login.code, 0);

            const RunResult add = run(
                {"add", "--drug", "Estradiol valerate", "--time", "now",
                 "--measurement", "2mg", "--route", "injection", "--mode",
                 "transfem", "--account-password-file", account.string(),
                 "--vault-password-stdin", "--config-dir",
                 temp.path().string()},
                kPasswordLine);
            CHECK_EQ(add.code, 0);
            CHECK(contains(add.out, "added "));
            CHECK(contains(add.out, "pushed "));
            CHECK_EQ(server.snapshot_puts(), 1);
            CHECK(contains(server.last_snapshot_body(), "\"cloud\":1"));
        }
    }
#endif

    return test_result();
}
