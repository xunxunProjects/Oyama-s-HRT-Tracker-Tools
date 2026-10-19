#include "commands.h"

#include "api.h"
#include "entropy.h"
#include "error.h"
#include "local_commands.h"
#include "log.h"
#include "paths.h"
#include "prompt.h"
#include "vault.h"
#include "version.h"

#include <algorithm>
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

int command_version(const ParsedArgs &parsed, std::ostream &out) {
    (void)parsed;
    out << format_version_line();
    return 0;
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
