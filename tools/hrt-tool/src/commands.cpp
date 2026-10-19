#include "commands.h"

#include "api.h"
#include "encrypted_file.h"
#include "entropy.h"
#include "error.h"
#include "local_commands.h"
#include "log.h"
#include "model.h"
#include "paths.h"
#include "prompt.h"
#include "snapshot.h"
#include "vault.h"
#include "version.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

bool has_argument(const std::vector<std::string> &args, const std::string &value) {
    return std::find(args.begin(), args.end(), value) != args.end();
}

VaultSession unlock_required(const GlobalOptions &options, std::istream &in,
                             std::ostream &out) {
    const Paths paths = resolve_paths(options.config_dir);
    if (!Vault::exists(paths.vault_file)) {
        throw ToolError(ExitCode::Vault, "VAULT_MISSING",
                        "vault not found; run hrt-tool init");
    }
    const std::string password = read_vault_password(options, in, out, false);
    return Vault::unlock(paths.vault_file, password);
}

struct UnlockedVault {
    VaultSession session;
    std::string master;
};

UnlockedVault unlock_full(const GlobalOptions &options, std::istream &in,
                          std::ostream &out) {
    const Paths paths = resolve_paths(options.config_dir);
    if (!Vault::exists(paths.vault_file)) {
        throw ToolError(ExitCode::Vault, "VAULT_MISSING",
                        "vault not found; run hrt-tool init");
    }
    std::string master = read_vault_password(options, in, out, false);
    VaultSession session = Vault::unlock(paths.vault_file, master);
    return UnlockedVault{std::move(session), std::move(master)};
}

std::string command_value(const std::vector<std::string> &args,
                          const std::string &name) {
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == name && i + 1 < args.size()) {
            return args[i + 1];
        }
        const std::string prefix = name + "=";
        if (args[i].compare(0, prefix.size(), prefix) == 0) {
            return args[i].substr(prefix.size());
        }
    }
    return {};
}

HttpOptions http_options_for(const ParsedArgs &parsed, std::ostream &err) {
    HttpOptions http;
    http.proxy = parsed.options.proxy;
    http.debug = parsed.options.debug;
    http.trace = parsed.options.trace;
    http.timeout_s = parsed.options.timeout_s;
    http.insecure_tls = parsed.options.insecure_tls;
    http.trace_out = &err;
    return http;
}

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string format_iso(int64_t seconds) {
    const std::time_t at = static_cast<std::time_t>(seconds);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &at);
#else
    gmtime_r(&at, &tm);
#endif
    char buffer[64] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::string read_username(const ParsedArgs &parsed, std::istream &in,
                          std::ostream &out) {
    std::string username = command_value(parsed.command_args, "--username");
    if (username.empty()) {
        out << "Username: ";
        out.flush();
        if (!std::getline(in, username)) {
            throw ToolError(ExitCode::Usage, "USERNAME", "username is required");
        }
        if (!username.empty() && username.back() == '\r') {
            username.pop_back();
        }
    }
    if (username.empty()) {
        throw ToolError(ExitCode::Usage, "USERNAME", "username is required");
    }
    return username;
}

std::string server_for(const ParsedArgs &parsed, const VaultData &data) {
    if (parsed.options.server_explicit) {
        return parsed.options.server;
    }
    return data.server.empty() ? parsed.options.server : data.server;
}

std::array<uint8_t, 32> cloud_key_for(UnlockedVault &unlocked,
                                     const ParsedArgs &parsed, std::istream &in,
                                     std::ostream &out) {
    if (!unlocked.session.data.cloud_key.empty()) {
        return cloud_key_from_hex(unlocked.session.data.cloud_key);
    }
    if (unlocked.session.data.user_id.empty()) {
        throw ApiAuthError("NO_USER", "the vault has no user id; log in again");
    }
    const std::string password = read_account_password(parsed.options, in, out);
    const std::array<uint8_t, 32> key =
        derive_cloud_key(password, unlocked.session.data.user_id);
    if (parsed.options.cache_cloud_key) {
        unlocked.session.data.cloud_key = cloud_key_to_hex(key);
        const Paths paths = resolve_paths(parsed.options.config_dir);
        Vault::save(paths.vault_file, unlocked.session.data, unlocked.master);
    }
    return key;
}

nlohmann::json payload_from_snapshot(const Snapshot &snapshot,
                                     const std::array<uint8_t, 32> &key) {
    if (is_cloud_bundle(snapshot.data)) {
        return decrypt_cloud_bundle(snapshot.data, key);
    }
    return snapshot.data;
}

nlohmann::json read_sync_input(const std::string &path, const std::string &master,
                               const std::array<uint8_t, 32> &key) {
    std::ifstream probe(path, std::ios::binary);
    if (!probe) {
        throw ToolError(ExitCode::Usage, "SNAPSHOT_INPUT",
                        "cannot open " + path);
    }
    char magic[4] = {};
    probe.read(magic, 4);
    probe.close();
    if (probe.gcount() == 4 && std::memcmp(magic, "HRTE", 4) == 0) {
        const std::string text =
            open_encrypted_file(path, "HRTE", master);
        nlohmann::json parsed =
            nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            throw ToolError(ExitCode::Data, "SNAPSHOT_INPUT",
                            "decrypted export is not a JSON object");
        }
        return parsed;
    }
    return load_snapshot_with_key(path, key);
}

int command_version(const ParsedArgs &parsed, std::ostream &out) {
    (void)parsed;
    out << format_version_line();
    return 0;
}

int command_login(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                  std::ostream &err) {
    UnlockedVault unlocked = unlock_full(parsed.options, in, out);
    const std::string username = read_username(parsed, in, out);
    const std::string password = read_account_password(parsed.options, in, out);
    const std::string server = server_for(parsed, unlocked.session.data);
    /* A device pass is a credential for one server; never send a cached one
       to a different origin. */
    const std::string device_pass =
        server == unlocked.session.data.server ? unlocked.session.data.device_pass
                                               : std::string{};

    std::unique_ptr<ApiClient> client =
        make_api_client(server, http_options_for(parsed, err));
    LoginResult result;
    try {
        result = client->login(username, password, "", "", device_pass);
    } catch (const ApiTwoFactorRequired &) {
        const std::string code =
            read_password(in, out, "2FA code (or backup code): ", false, true);
        if (code.empty()) {
            throw ToolError(ExitCode::Usage, "TWO_FACTOR", "a second factor is required");
        }
        const bool numeric =
            code.size() == 6 &&
            std::all_of(code.begin(), code.end(),
                        [](char c) { return c >= '0' && c <= '9'; });
        if (numeric) {
            result = client->login(username, password, code, "", device_pass);
        } else {
            result = client->login(username, password, "", code, device_pass);
        }
    }

    const std::array<uint8_t, 32> key =
        derive_cloud_key(password, result.user_id);
    unlocked.session.data.token = result.token;
    unlocked.session.data.user_id = result.user_id;
    unlocked.session.data.username = result.username;
    if (!result.device_pass.empty()) {
        unlocked.session.data.device_pass = result.device_pass;
    }
    unlocked.session.data.cloud_key =
        parsed.options.cache_cloud_key ? cloud_key_to_hex(key) : "";
    unlocked.session.data.server = server;
    if (!parsed.options.proxy.empty()) {
        unlocked.session.data.proxy = parsed.options.proxy;
    }
    unlocked.session.data.opsec_ack = true;
    unlocked.session.data.updated_at = now_ms();
    const Paths paths = resolve_paths(parsed.options.config_dir);
    Vault::save(paths.vault_file, unlocked.session.data, unlocked.master);

    if (parsed.options.json) {
        out << nlohmann::json{{"user", {{"id", result.user_id},
                                        {"username", result.username}}},
                              {"apiVersion", result.api_version}}
                   .dump(2)
            << "\n";
    } else {
        out << "logged in as " << result.username << " (" << result.user_id << ")\n";
    }
    return 0;
}

int command_logout(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                   std::ostream &err) {
    UnlockedVault unlocked = unlock_full(parsed.options, in, out);
    if (unlocked.session.data.token.empty()) {
        out << "not logged in\n";
        return 0;
    }
    std::unique_ptr<ApiClient> client = make_api_client(
        server_for(parsed, unlocked.session.data), http_options_for(parsed, err),
        unlocked.session.data.token);
    client->logout();

    unlocked.session.data.token.clear();
    unlocked.session.data.cloud_key.clear();
    unlocked.session.data.device_pass.clear();
    unlocked.session.data.updated_at = now_ms();
    const Paths paths = resolve_paths(parsed.options.config_dir);
    Vault::save(paths.vault_file, unlocked.session.data, unlocked.master);
    if (parsed.options.json) {
        out << "{\"ok\":true}\n";
    } else {
        out << "logged out\n";
    }
    return 0;
}

int command_whoami(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                   std::ostream &err) {
    UnlockedVault unlocked = unlock_full(parsed.options, in, out);
    if (unlocked.session.data.token.empty()) {
        throw ApiAuthError("NOT_LOGGED_IN", "log in first; run hrt-tool login");
    }
    std::unique_ptr<ApiClient> client = make_api_client(
        server_for(parsed, unlocked.session.data), http_options_for(parsed, err),
        unlocked.session.data.token);
    const nlohmann::json me = client->me();
    if (parsed.options.json) {
        out << me.dump(2) << "\n";
        return 0;
    }
    const nlohmann::json &user = me.value("user", nlohmann::json::object());
    out << user.value("username", std::string{}) << " ("
        << user.value("id", std::string{}) << ")\n";
    out << "server time: " << me.value("serverTime", std::string{}) << "\n";
    out << "api version: " << me.value("apiVersion", 1) << "\n";
    return 0;
}

int command_snapshot(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                     std::ostream &err) {
    if (parsed.command_args.empty()) {
        throw ToolError(ExitCode::Usage, "USAGE",
                        "snapshot needs pull, push, or list");
    }
    const std::string sub = parsed.command_args.front();
    UnlockedVault unlocked = unlock_full(parsed.options, in, out);
    if (unlocked.session.data.token.empty()) {
        throw ApiAuthError("NOT_LOGGED_IN", "log in first; run hrt-tool login");
    }
    std::unique_ptr<ApiClient> client = make_api_client(
        server_for(parsed, unlocked.session.data), http_options_for(parsed, err),
        unlocked.session.data.token);

    if (sub == "list") {
        const nlohmann::json list = client->list_snapshots();
        if (parsed.options.json) {
            out << list.dump(2) << "\n";
            return 0;
        }
        for (const nlohmann::json &row : list) {
            out << row.value("id", std::string{}) << "  "
                << format_iso(row.value("createdAt", int64_t{0})) << "  "
                << row.value("dataSize", int64_t{0}) << " bytes\n";
        }
        return 0;
    }

    if (sub == "pull") {
        const std::string out_path = command_value(parsed.command_args, "--out");
        if (out_path.empty()) {
            throw ToolError(ExitCode::Usage, "USAGE", "snapshot pull needs --out FILE");
        }
        const std::string id = command_value(parsed.command_args, "--id");
        std::optional<Snapshot> snapshot =
            id.empty() ? client->latest_snapshot() : std::optional<Snapshot>(client->snapshot_by_id(id));
        if (!snapshot.has_value()) {
            throw ToolError(ExitCode::Data, "NO_SNAPSHOT",
                            "the account has no snapshot yet");
        }
        const std::array<uint8_t, 32> key =
            cloud_key_for(unlocked, parsed, in, out);
        const nlohmann::json payload = payload_from_snapshot(*snapshot, key);
        if (out_path == "-") {
            out << payload.dump(2) << "\n";
        } else {
            if (std::filesystem::exists(out_path) &&
                !has_argument(parsed.command_args, "--force")) {
                throw ToolError(
                    ExitCode::Usage, "SNAPSHOT_EXISTS",
                    "refusing to overwrite " + out_path +
                        "; pass --force or choose another path");
            }
            save_snapshot_with_key(out_path, payload, key);
            out << "pulled " << snapshot->id << " (" << snapshot->created_at
                << ") -> " << out_path << "\n";
        }
        return 0;
    }

    if (sub == "push") {
        const std::string in_path = command_value(parsed.command_args, "--in");
        if (in_path.empty()) {
            throw ToolError(ExitCode::Usage, "USAGE", "snapshot push needs --in FILE");
        }
        const std::array<uint8_t, 32> key =
            cloud_key_for(unlocked, parsed, in, out);
        const nlohmann::json local = read_sync_input(in_path, unlocked.master, key);
        if (!local.is_object()) {
            throw ToolError(ExitCode::Data, "SNAPSHOT_INPUT",
                            "the input snapshot is not a sync payload object");
        }

        std::string base;
        nlohmann::json merged = local;
        std::optional<Snapshot> current = client->latest_snapshot();
        if (current.has_value()) {
            const nlohmann::json remote_payload =
                payload_from_snapshot(*current, key);
            if (fingerprint_state(normalize_sync_state(remote_payload)) ==
                fingerprint_state(normalize_sync_state(local))) {
                out << "already up to date (" << current->id << ")\n";
                return 0;
            }
            merged = merge_sync_payloads(remote_payload, local).payload;
            base = current->id;
        }
        for (int attempt = 0; attempt < 3; ++attempt) {
            try {
                const Snapshot pushed =
                    client->put_snapshot(encrypt_cloud_bundle(merged, key), base);
                out << "pushed " << pushed.id << " (" << pushed.created_at << ")\n";
                return 0;
            } catch (const SnapshotConflictError &conflict) {
                if (attempt == 2) {
                    throw ToolError(ExitCode::Data, "CONFLICT",
                                    "snapshot kept changing; try again");
                }
                std::optional<Snapshot> remote =
                    conflict.current_id().empty()
                        ? client->latest_snapshot()
                        : std::optional<Snapshot>(
                              client->snapshot_by_id(conflict.current_id()));
                if (!remote.has_value()) {
                    base.clear();
                    merged = local;
                    continue;
                }
                merged = merge_sync_payloads(payload_from_snapshot(*remote, key), local)
                             .payload;
                base = remote->id;
            }
        }
        throw ToolError(ExitCode::Data, "CONFLICT", "snapshot push did not converge");
    }

    throw ToolError(ExitCode::Usage, "USAGE", "snapshot needs pull, push, or list");
}

int command_init(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                 std::ostream &err) {
    const Paths paths = resolve_paths(parsed.options.config_dir);
    const bool reset = has_argument(parsed.command_args, "--reset");

    if (reset && Vault::exists(paths.vault_file) && !parsed.options.yes) {
        err << "Replace the existing vault? Type 'yes' to continue: ";
        err.flush();
        std::string answer;
        if (!std::getline(in, answer)) {
            throw ToolError(ExitCode::Usage, "RESET_ABORTED",
                            "vault replacement cancelled");
        }
        if (!answer.empty() && answer.back() == '\r') {
            answer.pop_back();
        }
        if (answer != "yes") {
            throw ToolError(ExitCode::Usage, "RESET_ABORTED",
                            "vault replacement cancelled");
        }
    }

    const std::string password = read_vault_password(parsed.options, in, out, true);
    const EntropyEstimate estimate = estimate_entropy(password);
    out << "master password: guess " << estimate.guess_bits << " bits, uniform "
        << estimate.uniform_bits << " bits, " << estimate.code_points
        << " code points, charset " << estimate.charset_size << "\n";
    out << "classes:";
    if (estimate.has_lower) out << " lower";
    if (estimate.has_upper) out << " upper";
    if (estimate.has_digits) out << " digits";
    if (estimate.has_symbols) out << " symbols";
    if (estimate.has_non_ascii) out << " non-ascii";
    out << "\n";
    if (parsed.options.debug) {
        out << "debug: empirical Shannon " << empirical_shannon_bits(password) << " bits\n";
    }

    VaultSession session = Vault::create(paths.vault_file, password, reset);
    (void)session;
    out << "vault created: " << paths.vault_file.string() << "\n";
    return 0;
}

int command_config_path(const ParsedArgs &parsed, std::ostream &out) {
    const Paths paths = resolve_paths(parsed.options.config_dir);
    out << "config_dir: " << paths.config_dir.string() << "\n";
    out << "vault: " << paths.vault_file.string() << "\n";
    return 0;
}

int command_logs_show(const ParsedArgs &parsed, std::istream &in, std::ostream &out) {
    if (parsed.options.log_file.empty()) {
        throw ToolError(ExitCode::Usage, "USAGE", "logs show requires --log-file FILE");
    }
    VaultSession session = unlock_required(parsed.options, in, out);
    EncryptedLog::show(parsed.options.log_file, session.log_key, out);
    return 0;
}

int command_stub(const std::string &name, const ParsedArgs &parsed, std::istream &in,
                 std::ostream &out, std::ostream &err) {
    VaultSession session = unlock_required(parsed.options, in, out);
    if (parsed.options.debug) {
        err << "debug: " << name << " (M1 stub)\n";
    }
    if (!parsed.options.log_file.empty() &&
        (parsed.options.debug || parsed.options.trace)) {
        EncryptedLog::append(parsed.options.log_file, session.log_key,
                             "debug: " + name + " invoked");
    }

    HttpOptions http;
    http.proxy = parsed.options.proxy;
    http.debug = parsed.options.debug;
    http.trace = parsed.options.trace;
    http.timeout_s = parsed.options.timeout_s;
    http.insecure_tls = parsed.options.insecure_tls;
    http.trace_out = &err;
    std::unique_ptr<ApiClient> client = make_api_client(parsed.options.server, http);

    if (name == "login") {
        (void)client->login("", "", "", "", session.data.device_pass);
    } else if (name == "logout") {
        client->logout();
    } else if (name == "whoami") {
        (void)client->me();
    }

    throw ToolError(ExitCode::NotImplemented, "NOT_IMPLEMENTED",
                    name + " is not implemented in M1");
}

const std::set<std::string> &known_commands() {
    static const std::set<std::string> kCommands = {
        "version", "init",    "login",    "logout", "whoami", "look",   "add",
        "dose",    "lab",     "weight",   "export", "import", "snapshot", "config",
        "logs",
    };
    return kCommands;
}

} // namespace

std::string read_vault_password(const GlobalOptions &options, std::istream &in,
                                std::ostream &out, bool confirm) {
    if (!options.vault_password_file.empty()) {
#ifndef _WIN32
        struct stat info {};
        if (::stat(options.vault_password_file.c_str(), &info) != 0) {
            throw ToolError(ExitCode::Vault, "VAULT_PASSWORD_FILE",
                            "cannot stat the vault password file");
        }
        if ((info.st_mode & 0077) != 0) {
            throw ToolError(ExitCode::Vault, "VAULT_PASSWORD_PERMISSIONS",
                            "vault password file must be mode 0600");
        }
#endif
        std::error_code size_error;
        const auto size =
            std::filesystem::file_size(options.vault_password_file, size_error);
        if (!size_error && size > kMaxMasterPasswordBytes + 2) {
            throw ToolLimitError("vault password file exceeds 256 bytes");
        }

        std::ifstream file(options.vault_password_file);
        if (!file) {
            throw ToolError(ExitCode::Vault, "VAULT_PASSWORD_FILE",
                            "cannot read the vault password file");
        }
        const std::string password = read_password_line(file, kMaxMasterPasswordBytes);
        if (password.empty()) {
            throw ToolError(ExitCode::Vault, "VAULT_PASSWORD_FILE",
                            "vault password file is empty");
        }
        return password;
    }

    if (options.vault_password_stdin) {
        const std::string password = read_password_line(in, kMaxMasterPasswordBytes);
        if (password.empty()) {
            throw ToolError(ExitCode::Vault, "VAULT_PASSWORD",
                            "vault password must not be empty");
        }
        return password;
    }

    return read_password(in, out, "Master password: ", confirm, true);
}

std::string read_account_password(const GlobalOptions &options, std::istream &in,
                                  std::ostream &out) {
    if (!options.account_password_file.empty()) {
#ifndef _WIN32
        struct stat info {};
        if (::stat(options.account_password_file.c_str(), &info) != 0) {
            throw ToolError(ExitCode::Vault, "ACCOUNT_PASSWORD_FILE",
                            "cannot stat the account password file");
        }
        if ((info.st_mode & 0077) != 0) {
            throw ToolError(ExitCode::Vault, "ACCOUNT_PASSWORD_PERMISSIONS",
                            "account password file must be mode 0600");
        }
#endif
        std::error_code size_error;
        const auto size =
            std::filesystem::file_size(options.account_password_file, size_error);
        if (!size_error && size > kMaxMasterPasswordBytes + 2) {
            throw ToolLimitError("account password file exceeds 256 bytes");
        }
        std::ifstream file(options.account_password_file);
        if (!file) {
            throw ToolError(ExitCode::Vault, "ACCOUNT_PASSWORD_FILE",
                            "cannot read the account password file");
        }
        const std::string password = read_password_line(file, kMaxMasterPasswordBytes);
        if (password.empty()) {
            throw ToolError(ExitCode::Vault, "ACCOUNT_PASSWORD_FILE",
                            "account password file is empty");
        }
        return password;
    }
    if (options.account_password_stdin) {
        const std::string password = read_password_line(in, kMaxMasterPasswordBytes);
        if (password.empty()) {
            throw ToolError(ExitCode::Vault, "ACCOUNT_PASSWORD",
                            "account password must not be empty");
        }
        return password;
    }
    return read_password(in, out, "Account password: ", false, true);
}

int dispatch(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
             std::ostream &err) {
    (void)err;

    if (!known_commands().count(parsed.command)) {
        throw ToolError(ExitCode::Usage, "USAGE", "unknown command: " + parsed.command);
    }

    if (parsed.command == "version") {
        return command_version(parsed, out);
    }
    if (parsed.command == "init") {
        return command_init(parsed, in, out, err);
    }
    if (parsed.command == "login") {
        return command_login(parsed, in, out, err);
    }
    if (parsed.command == "logout") {
        return command_logout(parsed, in, out, err);
    }
    if (parsed.command == "whoami") {
        return command_whoami(parsed, in, out, err);
    }
    if (parsed.command == "snapshot") {
        return command_snapshot(parsed, in, out, err);
    }
    if (parsed.command == "config") {
        if (!parsed.command_args.empty() && parsed.command_args.front() == "path") {
            return command_config_path(parsed, out);
        }
        const std::string name =
            parsed.command_args.empty() ? "config" : "config " + parsed.command_args.front();
        return command_stub(name, parsed, in, out, err);
    }
    if (parsed.command == "logs") {
        if (!parsed.command_args.empty() && parsed.command_args.front() == "show") {
            return command_logs_show(parsed, in, out);
        }
        const std::string name =
            parsed.command_args.empty() ? "logs" : "logs " + parsed.command_args.front();
        return command_stub(name, parsed, in, out, err);
    }

    if (is_local_command(parsed.command)) {
        return handle_local_command(parsed, in, out, err);
    }

    std::string name = parsed.command;
    if (!parsed.command_args.empty()) {
        name += " " + parsed.command_args.front();
    }
    return command_stub(name, parsed, in, out, err);
}
