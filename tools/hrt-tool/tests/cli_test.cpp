#include "cli.h"
#include "test_framework.h"

#include <cctype>
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
#include <thread>
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

#ifndef _WIN32
/* Loopback HTTP stub for the live trust flow; POSIX only, like the CLI tests
   that need real sockets. */
class LoopbackServer {
public:
    LoopbackServer() {
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
    }

    LoopbackServer(const LoopbackServer &) = delete;
    LoopbackServer &operator=(const LoopbackServer &) = delete;

    bool ok() const { return socket_ >= 0; }
    std::string url() const {
        return "http://127.0.0.1:" + std::to_string(port_);
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
            respond(client);
            ::close(client);
        }
    }

    static void respond(int client) {
        std::string request;
        char buffer[4096];
        bool headers_done = false;
        size_t header_end = 0;
        size_t content_length = 0;
        while (request.size() < 65536) {
            const ssize_t got = ::recv(client, buffer, sizeof(buffer), 0);
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

        const size_t first_space = request.find(' ');
        const size_t second_space =
            first_space == std::string::npos
                ? std::string::npos
                : request.find(' ', first_space + 1);
        const std::string path =
            first_space == std::string::npos || second_space == std::string::npos
                ? std::string{}
                : request.substr(first_space + 1, second_space - first_space - 1);

        std::string status = "200 OK";
        std::string body;
        if (path == "/api/tool/v1/version") {
            body = R"({"apiVersion":1,"minToolVersion":"0.1.0","transport":["tls"],)"
                   R"("serverTime":"2026-10-20T02:00:00.000Z"})";
        } else if (path == "/api/tool/v1/login") {
            body = R"({"token":"tok-test-1","user":{"id":"user-1",)"
                   R"("username":"Alice","isAdmin":false},)"
                   R"("devicePass":"dev-pass-1","apiVersion":1})";
        } else if (path == "/api/tool/v1/me") {
            body = R"({"user":{"id":"user-1","username":"Alice","isAdmin":false},)"
                   R"("serverTime":"2026-10-20T02:00:00.000Z","apiVersion":1})";
        } else if (path == "/api/tool/v1/logout") {
            body = R"({"ok":true})";
        } else {
            status = "404 Not Found";
            body = R"({"code":"NOT_FOUND","message":"no"})";
        }

        const std::string response =
            "HTTP/1.1 " + status +
            "\r\nContent-Type: application/json\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        ::send(client, response.data(), response.size(), MSG_NOSIGNAL);
    }

    int socket_ = -1;
    unsigned short port_ = 0;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};
#endif

} // namespace

int main() {
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

        for (const char *command : {"add", "dose", "lab", "weight", "export",
                                    "import"}) {
            std::vector<std::string> args{command};
            args.push_back("--config-dir");
            args.push_back(temp.path().string());
            args.push_back("--vault-password-stdin");
            const RunResult result = run(args, kPasswordLine);
            CHECK_EQ(result.code, 2);
            CHECK(contains(result.err, "--snapshot") ||
                  contains(result.err, "missing --out") ||
                  contains(result.err, "missing --in") ||
                  contains(result.err, "dose needs") ||
                  contains(result.err, "lab needs") ||
                  contains(result.err, "weight needs"));
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
                {"login", "--server", server.url(), "--username", "Alice",
                 "--account-password-file", account.string(),
                 "--vault-password-stdin", "--config-dir", temp.path().string()},
                std::string(kPasswordLine) + "yes\n");
            CHECK_EQ(login.code, 0);
            CHECK(contains(login.err, "first contact with " + server.url()));
            CHECK(contains(login.err, "plain HTTP"));
            CHECK(contains(login.out, "Alice"));

            /* The interactive yes pins the origin; the next command must not
               prompt again even though no --trust-server was given. */
            const RunResult whoami = run(
                {"whoami", "--vault-password-stdin",
                 "--config-dir", temp.path().string()},
                kPasswordLine);
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

        const RunResult set_proxy = run(
            {"config", "set", "proxy", "socks5h://127.0.0.1:9050",
             "--vault-password-stdin", "--config-dir", temp.path().string()},
            kPasswordLine);
        CHECK_EQ(set_proxy.code, 0);
        CHECK(contains(set_proxy.out, "proxy set"));
    }
#endif

    return test_result();
}
