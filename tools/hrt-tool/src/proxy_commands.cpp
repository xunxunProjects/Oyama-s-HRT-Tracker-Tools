#include "commands.h"

#include "error.h"
#include "http.h"
#include "paths.h"
#include "session.h"
#include "vault.h"

#include <string>

namespace {

void save_vault(const ParsedArgs &parsed, UnlockedVault &unlocked) {
    const Paths paths = resolve_paths(parsed.options.config_dir);
    Vault::save(paths.vault_file, unlocked.session().data, unlocked.master());
}

std::string network_name(const std::string &proxy) {
    if (proxy.empty()) {
        return "none";
    }
    if (proxy.find(":9050") != std::string::npos) {
        return "Tor";
    }
    if (proxy.find(":4447") != std::string::npos) {
        return "I2P";
    }
    return "custom";
}

} // namespace

int command_proxy(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                  std::ostream &err, SessionContext *context) {
    if (parsed.command_args.empty()) {
        throw ToolError(ExitCode::Usage, "USAGE",
                        "proxy needs tor, i2p, off, status, or test");
    }
    const std::string sub = parsed.command_args.front();
    UnlockedVault unlocked = unlock_full(parsed.options, in, out, context);
    VaultData &data = unlocked.session().data;

    if (sub == "tor") {
        data.proxy = "socks5h://127.0.0.1:9050";
        save_vault(parsed, unlocked);
        out << "proxy set: " << data.proxy << "\n";
        return 0;
    }
    if (sub == "i2p") {
        data.proxy = "socks5h://127.0.0.1:4447";
        save_vault(parsed, unlocked);
        out << "proxy set: " << data.proxy << "\n";
        return 0;
    }
    if (sub == "off" || sub == "none") {
        data.proxy.clear();
        save_vault(parsed, unlocked);
        out << "proxy unset\n";
        return 0;
    }
    if (sub == "status") {
        out << "proxy: " << (data.proxy.empty() ? "(none)" : data.proxy) << "\n";
        out << "network: " << network_name(data.proxy) << "\n";
        return 0;
    }
    if (sub == "test") {
        if (data.proxy.empty()) {
            throw ToolError(ExitCode::Usage, "PROXY_MISSING",
                            "no proxy configured; run hrt-tool proxy tor|i2p");
        }
        std::string base = data.server;
        while (!base.empty() && base.back() == '/') {
            base.pop_back();
        }
        HttpOptions http;
        http.proxy = data.proxy;
        http.timeout_s = 5;
        http.trace_out = &err;
        HttpClient client;
        const HttpResponse response =
            client.request("GET", base + "/api/tool/v1/version",
                           {"Accept: application/json"}, "", http);
        if (response.status != 200) {
            throw ToolError(ExitCode::Network, "PROXY_TEST",
                            "the proxy reached the server but it answered HTTP " +
                                std::to_string(response.status));
        }
        out << "proxy test: PASS (HTTP 200 via " << data.proxy << ")\n";
        return 0;
    }
    throw ToolError(ExitCode::Usage, "USAGE",
                    "proxy needs tor, i2p, off, status, or test");
}
