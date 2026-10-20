#include "cli.h"
#include "error.h"
#include "hrt_client.h"
#include "hrt_tls_stub.h"
#include "paths.h"
#include "test_framework.h"
#include "vault.h"

#include <sys/stat.h>

#include <atomic>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char *kVaultPassword = "correct horse battery staple";
const char *kAccountPassword = "another long account passphrase";

class TempDir {
public:
    TempDir() {
        std::string pattern =
            (std::filesystem::temp_directory_path() / "hrt-login-test-XXXXXX")
                .string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        char *created = ::mkdtemp(buffer.data());
        if (created != nullptr) {
            path_ = created;
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

void write_private_file(const std::filesystem::path &path, const std::string &text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text << "\n";
    file.close();
    ::chmod(path.c_str(), 0600);
}

struct RunResult {
    int code = 0;
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
    RunResult result;
    result.code = run_cli(static_cast<int>(argv.size()), argv.data(), in, out, err);
    result.out = out.str();
    result.err = err.str();
    return result;
}

std::string extract_code(const std::string &err) {
    const std::string marker = "Verification code (128 hex):\n";
    const std::string::size_type at = err.find(marker);
    if (at == std::string::npos || at + marker.size() + 128 > err.size()) {
        return {};
    }
    return err.substr(at + marker.size(), 128);
}

} // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN);

    {
        std::mutex mutex;
        std::string start_body;
        int starts = 0;
        int polls = 0;
        bool logout_seen = false;
        bool me_seen = false;

        HrtTlsStubServer server(
            [&](const nlohmann::json &frame) {
                const std::string op = frame.value("op", std::string{});
                const std::string body =
                    base64_decode(frame.value("body_base64", std::string{}));
                std::lock_guard<std::mutex> lock(mutex);
                if (op == "pair.start") {
                    ++starts;
                    start_body = body;
                    return HrtStubResponse{
                        200,
                        {{"request_id", std::string(32, 'a')},
                         {"poll_secret", std::string(64, 'b')},
                         {"expires_at", 4600000000LL},
                         {"verification_url",
                          "https://example.test/settings?hrtPair=" +
                              std::string(32, 'a')}}};
                }
                if (op == "pair.poll") {
                    ++polls;
                    return HrtStubResponse{
                        200,
                        {{"status", "approved"},
                         {"token", "header.payload.signature"},
                         {"user", {{"id", "user-1"}, {"username", "alice"}}},
                         {"device",
                          {{"fingerprint", std::string(64, 'c')},
                           {"name", "hrt-tool"}}}}};
                }
                if (op == "request") {
                    const std::string path = frame.value("path", std::string{});
                    if (path == "/api/tool/v1/logout") {
                        logout_seen = true;
                        return HrtStubResponse{200, {{"ok", true}}};
                    }
                    if (path == "/api/tool/v1/me") {
                        me_seen = true;
                        return HrtStubResponse{
                            200,
                            {{"user", {{"id", "user-1"}, {"username", "alice"}}}}};
                    }
                }
                return HrtStubResponse{404, {{"code", "NOT_FOUND"}}};
            },
            "login-stub");
        CHECK(server.ok());
        if (server.ok()) {
            TempDir temp;
            const Paths paths = resolve_paths(temp.path().string());
            Vault::create(paths.vault_file, kVaultPassword);
            const std::filesystem::path vault_password = temp.path() / "vault.pw";
            const std::filesystem::path account_password = temp.path() / "account.pw";
            write_private_file(vault_password, kVaultPassword);
            write_private_file(account_password, kAccountPassword);

            const std::vector<std::string> base{
                "--config-dir", temp.path().string(),
                "--vault-password-file", vault_password.string(),
                "--account-password-file", account_password.string(),
                "--server", server.url(),
                "--trust-server",
                "--json",
            };
            std::vector<std::string> login_args = base;
            login_args.insert(login_args.end(),
                              {"login", "--username", "alice"});
            const RunResult login = run(login_args);
            CHECK_EQ(login.code, 0);
            CHECK(login.err.find("Verification code (128 hex):") != std::string::npos);
            CHECK(login.err.find("Approve this device at:") != std::string::npos);
            CHECK(login.err.find("https://example.test/settings?hrtPair=") !=
                  std::string::npos);
            const std::string code = extract_code(login.err);
            CHECK_EQ(code.size(), static_cast<size_t>(128));
            CHECK_EQ(login.out.find("hrt/1") != std::string::npos, true);
            {
                std::lock_guard<std::mutex> lock(mutex);
                CHECK_EQ(starts, 1);
                CHECK_EQ(polls, 1);
                const nlohmann::json body = nlohmann::json::parse(start_body);
                CHECK_EQ(body.value("username", std::string{}), std::string("alice"));
                CHECK_EQ(body.value("code_hash", std::string{}), sha256_hex(code));
                CHECK(body.dump().find(code) == std::string::npos);
            }

            const VaultSession session =
                Vault::unlock(paths.vault_file, kVaultPassword);
            CHECK_EQ(session.data.token, std::string("header.payload.signature"));
            CHECK_EQ(session.data.user_id, std::string("user-1"));
            CHECK_EQ(session.data.username, std::string("alice"));
            CHECK_EQ(session.data.server, server.url());
            CHECK(!session.data.device_key_pem.empty());
            CHECK(!session.data.device_cert_pem.empty());
            CHECK_EQ(session.data.device_fingerprint.size(), static_cast<size_t>(64));
            CHECK_EQ(session.data.cloud_key.size(), static_cast<size_t>(64));
            CHECK(session.data.last_login_failed_at == 0);

            std::vector<std::string> whoami_args{
                "--config-dir", temp.path().string(),
                "--vault-password-file", vault_password.string(),
                "whoami",
            };
            const RunResult whoami = run(whoami_args);
            CHECK_EQ(whoami.code, 0);
            CHECK(whoami.out.find("alice") != std::string::npos);
            {
                std::lock_guard<std::mutex> lock(mutex);
                CHECK(me_seen);
            }

            std::vector<std::string> logout_args{
                "--config-dir", temp.path().string(),
                "--vault-password-file", vault_password.string(),
                "logout",
            };
            const RunResult logout = run(logout_args);
            CHECK_EQ(logout.code, 0);
            {
                std::lock_guard<std::mutex> lock(mutex);
                CHECK(logout_seen);
            }
            const VaultSession cleared =
                Vault::unlock(paths.vault_file, kVaultPassword);
            CHECK(cleared.data.token.empty());
            CHECK(!cleared.data.device_key_pem.empty());
            CHECK(!cleared.data.device_cert_pem.empty());
            CHECK_EQ(cleared.data.device_fingerprint.size(), static_cast<size_t>(64));

            const RunResult after = run(whoami_args);
            CHECK_EQ(after.code, static_cast<int>(ExitCode::Vault));
            CHECK(after.err.find("NOT_LOGGED_IN") != std::string::npos);
        }
    }

    {
        HrtTlsStubServer server(
            [](const nlohmann::json &frame) {
                if (frame.value("op", std::string{}) == "pair.start") {
                    return HrtStubResponse{
                        200,
                        {{"request_id", std::string(32, 'a')},
                         {"poll_secret", std::string(64, 'b')},
                         {"expires_at", 4600000000LL},
                         {"verification_url", "https://example.test/settings"}}};
                }
                return HrtStubResponse{200, {{"status", "denied"}}};
            },
            "login-denied-stub");
        CHECK(server.ok());
        if (server.ok()) {
            TempDir temp;
            const Paths paths = resolve_paths(temp.path().string());
            Vault::create(paths.vault_file, kVaultPassword);
            const std::filesystem::path vault_password = temp.path() / "vault.pw";
            const std::filesystem::path account_password = temp.path() / "account.pw";
            write_private_file(vault_password, kVaultPassword);
            write_private_file(account_password, kAccountPassword);
            std::vector<std::string> args{
                "--config-dir", temp.path().string(),
                "--vault-password-file", vault_password.string(),
                "--account-password-file", account_password.string(),
                "--server", server.url(),
                "--trust-server",
                "login", "--username", "alice",
            };
            const RunResult denied = run(args);
            if (denied.code != static_cast<int>(ExitCode::Vault)) {
                std::cerr << "denied stderr: " << denied.err << "\n";
            }
            CHECK_EQ(denied.code, static_cast<int>(ExitCode::Vault));
            CHECK(denied.err.find("PAIR_DENIED") != std::string::npos);

            const RunResult cooldown = run(args);
            if (cooldown.code != static_cast<int>(ExitCode::Vault)) {
                std::cerr << "cooldown stderr: " << cooldown.err << "\n";
            }
            CHECK_EQ(cooldown.code, static_cast<int>(ExitCode::Vault));
            CHECK(cooldown.err.find("try again in") != std::string::npos);
            size_t starts = 0;
            for (const nlohmann::json &frame : server.frames()) {
                if (frame.value("op", std::string{}) == "pair.start") {
                    ++starts;
                }
            }
            CHECK_EQ(starts, static_cast<size_t>(1));
        }
    }

    {
        TempDir temp;
        const Paths paths = resolve_paths(temp.path().string());
        Vault::create(paths.vault_file, kVaultPassword);
        const std::filesystem::path vault_password = temp.path() / "vault.pw";
        write_private_file(vault_password, kVaultPassword);
        std::vector<std::string> args{
            "--config-dir", temp.path().string(),
            "--vault-password-file", vault_password.string(),
            "--server", "https://example.test",
            "--require-hrt",
            "login", "--username", "alice",
        };
        const RunResult required = run(args);
        CHECK_EQ(required.code, static_cast<int>(ExitCode::Usage));
        CHECK(required.err.find("HRT_REQUIRED") != std::string::npos);

        std::vector<std::string> config_base{
            "--config-dir", temp.path().string(),
            "--vault-password-file", vault_password.string(),
            "config",
        };
        std::vector<std::string> set = config_base;
        set.insert(set.end(), {"set", "server", "hrt://127.0.0.1:9443"});
        const RunResult set_result = run(set);
        CHECK_EQ(set_result.code, 0);
        CHECK(set_result.out.find("hrt://127.0.0.1:9443") != std::string::npos);

        std::vector<std::string> get = config_base;
        get.insert(get.end(), {"get", "server"});
        const RunResult get_result = run(get);
        CHECK_EQ(get_result.code, 0);
        CHECK(get_result.out.find("hrt://127.0.0.1:9443") != std::string::npos);

        std::vector<std::string> bad = config_base;
        bad.insert(bad.end(), {"set", "server", "hrt://127.0.0.1:9443/path"});
        const RunResult bad_result = run(bad);
        CHECK_EQ(bad_result.code, static_cast<int>(ExitCode::Usage));
        CHECK(bad_result.err.find("HRT_URL") != std::string::npos);

        const auto login_with_server =
            [&](const std::string &server_url,
                const std::vector<std::string> &extra) {
                std::vector<std::string> login{
                    "--config-dir", temp.path().string(),
                    "--vault-password-file", vault_password.string(),
                    "--server", server_url,
                };
                login.insert(login.end(), extra.begin(), extra.end());
                login.insert(login.end(), {"login", "--username", "alice"});
                return run(login);
            };

        const std::string onion = std::string(56, 'a') + ".onion";
        const RunResult missing_tor =
            login_with_server("hrt://" + onion, {});
        CHECK_EQ(missing_tor.code, static_cast<int>(ExitCode::Network));
        CHECK(missing_tor.err.find("TOR_PROXY_MISSING") != std::string::npos);
        CHECK(missing_tor.err.find("config set proxy socks5h://127.0.0.1:9050") !=
              std::string::npos);

        const RunResult weak_tor = login_with_server(
            "hrt://" + onion, {"--proxy", "socks5://127.0.0.1:9050"});
        CHECK_EQ(weak_tor.code, static_cast<int>(ExitCode::Usage));
        CHECK(weak_tor.err.find("PROXY_DNS") != std::string::npos);

        const RunResult bad_onion =
            login_with_server("hrt://short.onion", {});
        CHECK_EQ(bad_onion.code, static_cast<int>(ExitCode::Usage));
        CHECK(bad_onion.err.find("ONION_ADDRESS") != std::string::npos);

        const std::string b32 = std::string(52, 'b') + ".b32.i2p";
        const RunResult missing_i2p =
            login_with_server("hrt://" + b32, {});
        CHECK_EQ(missing_i2p.code, static_cast<int>(ExitCode::Network));
        CHECK(missing_i2p.err.find("I2P_PROXY_MISSING") != std::string::npos);
        CHECK(missing_i2p.err.find("config set proxy socks5h://127.0.0.1:4447") !=
              std::string::npos);
    }

    return test_result();
}
