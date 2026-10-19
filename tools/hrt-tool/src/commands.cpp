#include "commands.h"

#include "error.h"
#include "log.h"
#include "paths.h"
#include "prompt.h"
#include "vault.h"
#include "version.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

bool has_argument(const std::vector<std::string> &args, const std::string &value) {
    return std::find(args.begin(), args.end(), value) != args.end();
}

std::string strip_carriage_return(std::string value) {
    if (!value.empty() && value.back() == '\r') {
        value.pop_back();
    }
    return value;
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

int command_init(const ParsedArgs &parsed, std::istream &in, std::ostream &out) {
    const Paths paths = resolve_paths(parsed.options.config_dir);
    const bool reset = has_argument(parsed.command_args, "--reset");
    const std::string password = read_vault_password(parsed.options, in, out, true);
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
                 std::ostream &out) {
    VaultSession session = unlock_required(parsed.options, in, out);
    if (!parsed.options.log_file.empty() &&
        (parsed.options.debug || parsed.options.trace)) {
        EncryptedLog::append(parsed.options.log_file, session.log_key,
                             "debug: " + name + " invoked");
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
        std::ifstream file(options.vault_password_file);
        if (!file) {
            throw ToolError(ExitCode::Vault, "VAULT_PASSWORD_FILE",
                            "cannot read the vault password file");
        }
        std::string password;
        if (!std::getline(file, password)) {
            throw ToolError(ExitCode::Vault, "VAULT_PASSWORD_FILE",
                            "vault password file is empty");
        }
        password = strip_carriage_return(password);
        if (password.empty()) {
            throw ToolError(ExitCode::Vault, "VAULT_PASSWORD_FILE",
                            "vault password file is empty");
        }
        return password;
    }

    if (options.vault_password_stdin) {
        std::string password;
        if (!std::getline(in, password)) {
            throw ToolError(ExitCode::Vault, "VAULT_PASSWORD",
                            "no vault password was provided on stdin");
        }
        password = strip_carriage_return(password);
        if (password.empty()) {
            throw ToolError(ExitCode::Vault, "VAULT_PASSWORD",
                            "vault password must not be empty");
        }
        return password;
    }

    return read_password(in, out, "Master password: ", confirm, true);
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
        return command_init(parsed, in, out);
    }
    if (parsed.command == "config") {
        if (!parsed.command_args.empty() && parsed.command_args.front() == "path") {
            return command_config_path(parsed, out);
        }
        const std::string name =
            parsed.command_args.empty() ? "config" : "config " + parsed.command_args.front();
        return command_stub(name, parsed, in, out);
    }
    if (parsed.command == "logs") {
        if (!parsed.command_args.empty() && parsed.command_args.front() == "show") {
            return command_logs_show(parsed, in, out);
        }
        const std::string name =
            parsed.command_args.empty() ? "logs" : "logs " + parsed.command_args.front();
        return command_stub(name, parsed, in, out);
    }

    std::string name = parsed.command;
    if (!parsed.command_args.empty()) {
        name += " " + parsed.command_args.front();
    }
    return command_stub(name, parsed, in, out);
}
