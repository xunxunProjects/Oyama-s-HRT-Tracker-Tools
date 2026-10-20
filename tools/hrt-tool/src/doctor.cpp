#include "commands.h"

#include "error.h"
#include "http.h"
#include "paths.h"
#include "session.h"
#include "vault.h"

#include "hrtcore/hrtcore.h"
#include "hrtprint/hrtprint.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

struct Check {
    std::string name;
    std::string status;
    std::string detail;
};

void add_check(std::vector<Check> &checks, const std::string &name, bool ok,
               const std::string &detail, bool warn_only = false) {
    checks.push_back(Check{name, ok ? "PASS" : (warn_only ? "WARN" : "FAIL"),
                           detail});
}

bool env_proxy_present() {
    const char *names[] = {"http_proxy", "https_proxy", "ALL_PROXY",
                           "all_proxy"};
    for (const char *name : names) {
        const char *value = std::getenv(name);
        if (value != nullptr && *value != '\0') {
            return true;
        }
    }
    return false;
}

} // namespace

int command_doctor(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                   std::ostream &err, SessionContext *context) {
    (void)err;
    const Paths paths = resolve_paths(parsed.options.config_dir);
    std::vector<Check> checks;

    const bool vault_exists = Vault::exists(paths.vault_file);
    add_check(checks, "vault", vault_exists,
              vault_exists ? paths.vault_file.string() : "missing; run hrt-tool init");
#ifndef _WIN32
    if (vault_exists) {
        struct stat info {};
        if (::stat(paths.vault_file.c_str(), &info) == 0) {
            const bool private_mode = (info.st_mode & 0077) == 0;
            add_check(checks, "vault mode", private_mode,
                      private_mode ? "0600" : "group/other readable",
                      true);
        }
    }
#endif
    add_check(checks, "config dir", std::filesystem::is_directory(paths.config_dir),
              paths.config_dir.string());

    std::unique_ptr<UnlockedVault> unlocked;
    if (vault_exists) {
        try {
            unlocked = std::make_unique<UnlockedVault>(
                unlock_full(parsed.options, in, out, context));
        } catch (const ToolError &) {
            /* The vault status line already explains the missing/unlockable
               vault; do not print the password or the underlying error. */
        }
    }

    if (unlocked == nullptr) {
        add_check(checks, "proxy", false, "vault not unlocked", true);
        add_check(checks, "server", false, "vault not unlocked", true);
        add_check(checks, "trust pins", false, "vault not unlocked", true);
        add_check(checks, "payload", false, "vault not unlocked", true);
    } else {
        const VaultData &data = unlocked->session().data;
        if (data.proxy.empty()) {
            add_check(checks, "proxy", false, "not configured", true);
        } else {
            try {
                const std::string normalized =
                    normalize_proxy_url(data.proxy, nullptr);
                add_check(checks, "proxy", true, normalized);
            } catch (const ToolError &error) {
                add_check(checks, "proxy", false, error.what());
            }
        }
        try {
            validate_server_target(data.server);
            add_check(checks, "server", true, data.server);
        } catch (const ToolError &error) {
            add_check(checks, "server", false, error.what());
        }
        const size_t pins =
            data.trust.is_object() ? data.trust.size() : static_cast<size_t>(0);
        add_check(checks, "trust pins", true, std::to_string(pins) + " pinned");
        add_check(checks, "payload", data.version >= 2,
                  "version " + std::to_string(data.version));
    }

    add_check(checks, "core", true, hrtcore_version());
    add_check(checks, "printer", true, hrtprint_version());
    if (env_proxy_present()) {
        add_check(checks, "environment proxy", false,
                  "set but ignored by hrt-tool", true);
    } else {
        add_check(checks, "environment proxy", true, "none set");
    }

    if (parsed.options.json) {
        nlohmann::json result = nlohmann::json::array();
        for (const Check &check : checks) {
            result.push_back({{"name", check.name},
                              {"status", check.status},
                              {"detail", check.detail}});
        }
        out << nlohmann::json{{"checks", result}}.dump(2) << "\n";
        return 0;
    }

    for (const Check &check : checks) {
        out << check.name << ": " << check.status << " (" << check.detail
            << ")\n";
    }
    return 0;
}
