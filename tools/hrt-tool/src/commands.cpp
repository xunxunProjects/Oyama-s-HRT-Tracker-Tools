#include "commands.h"

#include "add_interactive.h"
#include "api.h"
#include "backup_envelope.h"
#include "cli.h"
#include "command_flags.h"
#include "completion.h"
#include "console.h"
#include "encrypted_file.h"
#include "link.h"
#include "entropy.h"
#include "error.h"
#include "hrt_client.h"
#include "hrt_pairing.h"
#include "local_commands.h"
#include "log.h"
#include "model.h"
#include "net_error.h"
#include "paths.h"
#include "prompt.h"
#include "session.h"
#include "snapshot.h"
#include "status.h"
#include "trust.h"
#include "vault.h"
#include "version.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

bool has_argument(const std::vector<std::string> &args, const std::string &value) {
    return std::find(args.begin(), args.end(), value) != args.end();
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

HttpOptions http_options_for(const ParsedArgs &parsed, const VaultData &data,
                             std::ostream &err) {
    HttpOptions http;
    if (parsed.options.proxy_explicit) {
        http.proxy = normalize_proxy_url(parsed.options.proxy, &err);
    } else if (!data.proxy.empty()) {
        /* A vault written by an older build may still hold socks5://. */
        http.proxy = normalize_proxy_url(data.proxy, nullptr);
    }
    http.debug = parsed.options.debug;
    http.trace = parsed.options.trace;
    http.timeout_s = parsed.options.timeout_s;
    http.insecure_tls = parsed.options.insecure_tls;
    http.trace_out = &err;
    http.hrt_device_cert_pem = data.device_cert_pem;
    http.hrt_device_key_pem = data.device_key_pem;
    return http;
}

std::string lower_ascii(std::string value) {
    for (char &c : value) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return value;
}

std::string normalized_origin(const std::string &url) {
    const Url parsed = parse_url(url);
    std::string port = parsed.port;
    if (port.empty()) {
        const std::string scheme = lower_ascii(parsed.scheme);
        port = scheme == "https" ? "443" : scheme == "hrt" ? "9443" : "80";
    }
    return lower_ascii(parsed.scheme) + "://" + lower_ascii(parsed.host) + ":" +
           port;
}

bool same_origin(const std::string &a, const std::string &b) {
    try {
        return normalized_origin(a) == normalized_origin(b);
    } catch (const ToolError &) {
        return false;
    }
}

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/* Hardcoded by design: a failed credential attempt buys a one-minute pause,
   so an interactive user cannot hammer the account. The server still owns
   the real rate limit; this is a local guard. */
constexpr int64_t kLoginFailureCooldownMs = 60 * 1000;

void enforce_login_cooldown(const VaultData &data) {
    if (data.last_login_failed_at <= 0) {
        return;
    }
    const int64_t now = now_ms();
    /* A clock that moved backwards must not lock the vault forever. */
    if (now < data.last_login_failed_at) {
        return;
    }
    const int64_t elapsed = now - data.last_login_failed_at;
    if (elapsed >= kLoginFailureCooldownMs) {
        return;
    }
    const int64_t seconds =
        (kLoginFailureCooldownMs - elapsed + 999) / 1000;
    throw ToolError(ExitCode::Auth, "LOGIN_COOLDOWN",
                    "a login attempt failed recently; try again in " +
                        std::to_string(seconds) + " seconds");
}

void save_login_failure(UnlockedVault &unlocked, const ParsedArgs &parsed) {
    unlocked.session().data.last_login_failed_at = now_ms();
    unlocked.session().data.updated_at = now_ms();
    const Paths paths = resolve_paths(parsed.options.config_dir);
    Vault::save(paths.vault_file, unlocked.session().data, unlocked.master());
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

/*
 * A saved JWT belongs to the saved origin. An explicit --server may only
 * differ for `login`; every authenticated command must use the saved origin
 * rather than send the token (and then report success/failure) elsewhere.
 */
std::string authenticated_server(const ParsedArgs &parsed, const VaultData &data) {
    const std::string saved =
        data.server.empty() ? parsed.options.server : data.server;
    if (!parsed.options.server_explicit) {
        return saved;
    }
    if (!same_origin(parsed.options.server, saved)) {
        throw ToolError(ExitCode::Trust, "SERVER_MISMATCH",
                        "the saved session belongs to " + saved +
                            "; run hrt-tool login --server " +
                            parsed.options.server + " to switch");
    }
    return saved;
}

std::string ensure_hrt_trust(UnlockedVault &unlocked, const ParsedArgs &parsed,
                             std::istream &in, std::ostream &err,
                             const std::string &server) {
    const Url parsed_url = parse_url(server);
    if (parsed_url.scheme != "hrt") {
        throw ToolError(ExitCode::Usage, "HRT_URL", "server must use hrt://");
    }
    validate_hidden_service_host(parsed_url);
    const bool onion = is_onion_host(parsed_url.host);
    const bool i2p = is_i2p_host(parsed_url.host);
    HttpOptions http = http_options_for(parsed, unlocked.session().data, err);
    if (onion || i2p) {
        const std::string network = onion ? "Tor" : "I2P";
        const std::string suggested = suggested_proxy_for_host(parsed_url.host);
        if (http.proxy.empty()) {
            throw ToolError(ExitCode::Network,
                            onion ? "TOR_PROXY_MISSING" : "I2P_PROXY_MISSING",
                            std::string("this is a ") + network + " address but no " +
                                network + " proxy is configured. Run: hrt-tool "
                                "config set proxy " + suggested);
        }
        if (lower_ascii(parse_url(http.proxy).scheme) != "socks5h") {
            throw ToolError(ExitCode::Trust, "PROXY_DNS",
                            std::string("hidden-service traffic must use a socks5h:// "
                                        "proxy. Run: hrt-tool config set proxy ") +
                                suggested);
        }
    }

    HrtClientOptions options;
    options.proxy = http.proxy;
    options.timeout_s = parsed.options.timeout_s;
    HrtClient probe(server, options);
    const HrtServerCertificate certificate = probe.connect();
    probe.close();
    const std::string pin = certificate.spki_sha256;
    if (pin.empty()) {
        throw ToolError(ExitCode::Trust, "TRUST_NO_PIN",
                        "could not compute the raw server public-key pin");
    }

    const std::string origin = normalized_origin(server);
    TrustStore store(unlocked.session().data.trust);
    const TrustState state = store.check(origin, pin);
    if (parsed.options.insecure_tls) {
        err << "warning: --insecure-tls does not apply to hrt://; the pinned "
               "public key is the trust anchor\n";
    }
    if (state == TrustState::Trusted) {
        return pin;
    }
    if (state == TrustState::Changed) {
        err << "warning: the pinned key for " << origin
            << " changed. This can mean a machine-in-the-middle or a server key "
               "rotation.\n";
        if (!parsed.options.trust_server) {
            throw ToolError(ExitCode::Trust, "TRUST_CHANGED",
                            "refusing the changed key; re-trust explicitly with "
                            "--trust-server");
        }
    } else {
        err << "first contact with " << origin << "\n";
        err << "  transport: TLS 1.3 (hrt/1), pinned public key; no public CA\n";
        err << "  certificate subject: "
            << (certificate.subject.empty() ? "(unavailable)" : certificate.subject)
            << "\n";
        err << "  issuer: "
            << (certificate.issuer.empty() ? "(unavailable)" : certificate.issuer)
            << "\n";
        err << "  valid: " << certificate.not_before << " .. "
            << certificate.not_after << "\n";
        err << "  SPKI SHA-256: " << pin << "\n";
        if (onion) {
            err << "warning: this is a Tor onion service. Verify the address and "
                   "the key out of band before entering credentials.\n";
        }
        if (i2p) {
            err << "warning: this is an I2P service. Verify the destination out "
                   "of band before entering credentials.\n";
        }
    }

    bool persist = false;
    bool accepted = false;
    if (parsed.options.trust_server) {
        accepted = true;
        persist = true;
    } else if (parsed.options.yes) {
        accepted = true;
        err << "warning: trusting " << origin << " for this run only (--yes)\n";
    } else {
        err << "Trust this server? Type 'yes' to continue: ";
        err.flush();
        std::string answer;
        if (!std::getline(in, answer)) {
            throw ToolError(ExitCode::Trust, "TRUST_REFUSED",
                            "server was not trusted");
        }
        if (!answer.empty() && answer.back() == '\r') {
            answer.pop_back();
        }
        accepted = answer == "yes";
        persist = accepted;
    }
    if (!accepted) {
        throw ToolError(ExitCode::Trust, "TRUST_REFUSED",
                        "server was not trusted");
    }
    if (persist) {
        store.trust(origin, pin);
        unlocked.session().data.updated_at = now_ms();
        const Paths paths = resolve_paths(parsed.options.config_dir);
        Vault::save(paths.vault_file, unlocked.session().data, unlocked.master());
        err << "trusted and pinned " << origin << "\n";
    }
    return pin;
}

std::string ensure_trust(UnlockedVault &unlocked, const ParsedArgs &parsed,
                         std::istream &in, std::ostream &err,
                         const std::string &server) {
    if (is_hrt_url(server)) {
        return ensure_hrt_trust(unlocked, parsed, in, err, server);
    }
    const Url parsed_url = parse_url(server);
    validate_server_url(parsed_url);
    validate_hidden_service_host(parsed_url);
    const bool onion = is_onion_host(parsed_url.host);
    const bool i2p = is_i2p_host(parsed_url.host);

    HttpOptions http = http_options_for(parsed, unlocked.session().data, err);
    if (onion || i2p) {
        /* Decide routing before any lookup: a .onion name must never reach the
           local resolver, and only socks5h keeps DNS at the proxy. */
        const std::string network = onion ? "Tor" : "I2P";
        const std::string suggested = suggested_proxy_for_host(parsed_url.host);
        const std::string set_proxy =
            "hrt-tool config set proxy " + suggested;
        if (http.proxy.empty()) {
            throw ToolError(ExitCode::Network,
                            onion ? "TOR_PROXY_MISSING" : "I2P_PROXY_MISSING",
                            std::string("this is a ") + network + " address but no " +
                                network + " proxy is configured. Run: " + set_proxy);
        }
        if (lower_ascii(parse_url(http.proxy).scheme) != "socks5h") {
            throw ToolError(
                ExitCode::Trust, "PROXY_DNS",
                std::string("hidden-service traffic must use a socks5h:// proxy "
                            "so DNS is resolved by the ") +
                    network + " proxy. Run: " + set_proxy);
        }
    }

    std::string base = server;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }

    HttpClient client;
    HttpResponse response;
    try {
        response = client.request("GET", base + "/api/tool/v1/version",
                                  {"Accept: application/json"}, "", http);
    } catch (const ToolError &error) {
        if (onion || i2p) {
            const std::string network = onion ? "Tor" : "I2P";
            const std::string suggested = suggested_proxy_for_host(parsed_url.host);
            throw ToolError(
                ExitCode::Network,
                onion ? "TOR_PROXY_FAILED" : "I2P_PROXY_FAILED",
                std::string("could not resolve ") + parsed_url.host +
                    " through the " + network + " proxy " + http.proxy +
                    ". Check that " + network +
                    " is running; reconfigure with: hrt-tool config set proxy " +
                    suggested + " (underlying error: " + error.what() + ")");
        }
        throw;
    }
    if (response.status != 200) {
        throw ToolError(ExitCode::Network, "SERVER_PROBE",
                        "the server did not answer the version probe (HTTP " +
                            std::to_string(response.status) + ")");
    }

    const bool tls = lower_ascii(parsed_url.scheme) == "https";
    if (tls && response.spki_sha256.empty()) {
        throw ToolError(ExitCode::Trust, "TRUST_NO_PIN",
                        "could not compute the server public-key pin");
    }
    const std::string pin = tls ? response.spki_sha256 : "none";
    const std::string origin = normalized_origin(server);
    TrustStore store(unlocked.session().data.trust);
    const TrustState state = store.check(origin, pin);

    if (parsed.options.insecure_tls && tls) {
        err << "warning: TLS certificate verification is disabled for this run\n";
    }
    if (!tls) {
        err << "warning: plain HTTP to " << origin
            << (onion || i2p
                    ? "; transport privacy comes only from the "
                      "hidden-service network, not from TLS\n"
                    : "; traffic is not encrypted on this network path\n");
    }

    if (state == TrustState::Trusted) {
        return tls ? pin : std::string{};
    }

    if (state == TrustState::Changed) {
        err << "warning: the pinned key for " << origin
            << " changed. This can mean a machine-in-the-middle or a server key "
               "rotation.\n";
        if (!parsed.options.trust_server) {
            throw ToolError(ExitCode::Trust, "TRUST_CHANGED",
                            "refusing the changed key; re-trust explicitly with "
                            "--trust-server");
        }
    } else {
        err << "first contact with " << origin << "\n";
        if (tls) {
            err << "  certificate subject: "
                << (response.cert_subject.empty() ? "(unavailable)"
                                                  : response.cert_subject)
                << "\n";
            err << "  issuer: "
                << (response.cert_issuer.empty() ? "(unavailable)"
                                                 : response.cert_issuer)
                << "\n";
            err << "  valid: " << response.cert_not_before << " .. "
                << response.cert_not_after << "\n";
            err << "  SPKI SHA-256: " << pin << "\n";
        } else {
            err << "  transport: plain HTTP (no certificate)\n";
        }
        if (lower_ascii(http.proxy).find("socks5h://") == 0) {
            err << "  DNS: resolved by the proxy\n";
        }
        if (onion) {
            err << "warning: this is a Tor onion service. Anyone can publish an "
                   "onion address; verify it out of band before entering "
                   "credentials. The address can change hands.\n";
        }
        if (i2p) {
            err << "warning: this is an I2P service. Verify the destination out "
                   "of band before entering credentials.\n";
        }
    }

    bool persist = false;
    bool accepted = false;
    if (parsed.options.trust_server) {
        accepted = true;
        persist = true;
    } else if (parsed.options.yes) {
        accepted = true;
        err << "warning: trusting " << origin
            << " for this run only (--yes)\n";
    } else {
        err << "Trust this server? Type 'yes' to continue: ";
        err.flush();
        std::string answer;
        if (!std::getline(in, answer)) {
            throw ToolError(ExitCode::Trust, "TRUST_REFUSED",
                            "server was not trusted");
        }
        if (!answer.empty() && answer.back() == '\r') {
            answer.pop_back();
        }
        accepted = answer == "yes";
        persist = accepted;
    }
    if (!accepted) {
        throw ToolError(ExitCode::Trust, "TRUST_REFUSED",
                        "server was not trusted");
    }

    if (persist) {
        store.trust(origin, pin);
        unlocked.session().data.updated_at = now_ms();
        const Paths paths = resolve_paths(parsed.options.config_dir);
        Vault::save(paths.vault_file, unlocked.session().data, unlocked.master());
        err << "trusted and pinned " << origin << "\n";
    }
    return tls ? pin : std::string{};
}

std::array<uint8_t, 32> cloud_key_for(UnlockedVault &unlocked,
                                     const ParsedArgs &parsed, std::istream &in,
                                     std::ostream &out) {
    if (!unlocked.session().data.cloud_key.empty()) {
        return cloud_key_from_hex(unlocked.session().data.cloud_key);
    }
    if (unlocked.session().data.user_id.empty()) {
        throw ApiAuthError("NO_USER", "the vault has no user id; log in again");
    }
    const std::string password = read_account_password(parsed.options, in, out);
    const std::array<uint8_t, 32> key =
        derive_cloud_key(password, unlocked.session().data.user_id);
    if (parsed.options.cache_cloud_key) {
        unlocked.session().data.cloud_key = cloud_key_to_hex(key);
        const Paths paths = resolve_paths(parsed.options.config_dir);
        Vault::save(paths.vault_file, unlocked.session().data, unlocked.master());
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
    if (parsed.options.json) {
        out << format_version_json() << "\n";
        return 0;
    }
    out << format_version_line();
    return 0;
}

int command_help(const ParsedArgs &parsed, std::ostream &out) {
    if (!parsed.command_args.empty() &&
        parsed.command_args.front() == "exit-codes") {
        out << "Exit codes:\n";
        for (int value = 0; value <= 14; ++value) {
            const ExitCode code = static_cast<ExitCode>(value);
            out << "  " << value << "  " << exit_code_name(code) << "  "
                << exit_code_description(code) << "\n";
        }
        return 0;
    }
    print_help(out);
    return 0;
}

int command_completion(const ParsedArgs &parsed, std::ostream &out) {
    if (parsed.command_args.empty()) {
        throw ToolError(ExitCode::Usage, "USAGE",
                        "completion needs bash, zsh, or fish");
    }
    return render_completion(parsed.command_args.front(), out);
}

std::string pairing_device_label(const ParsedArgs &parsed) {
    std::string name = parsed.options.device_name;
    if (name.empty()) {
        name = "hrt-tool";
    }
    const bool valid =
        name.size() <= 64 &&
        std::none_of(name.begin(), name.end(), [](unsigned char c) {
            return c < 0x20 || c == 0x7f;
        });
    if (!valid) {
        throw ToolError(ExitCode::Usage, "DEVICE_NAME",
                        "the device name must be 1-64 printable characters");
    }
    return name;
}

int login_hrt(UnlockedVault &unlocked, const ParsedArgs &parsed,
              const std::string &username, const std::string &server,
              std::istream &in, std::ostream &out, std::ostream &err) {
    const bool server_changed =
        !unlocked.session().data.server.empty() &&
        !same_origin(server, unlocked.session().data.server);
    if (server_changed) {
        unlocked.session().data.token.clear();
        unlocked.session().data.cloud_key.clear();
        unlocked.session().data.device_pass.clear();
        unlocked.session().data.device_key_pem.clear();
        unlocked.session().data.device_cert_pem.clear();
        unlocked.session().data.device_fingerprint.clear();
    }

    const std::string device_name = pairing_device_label(parsed);
    HrtDeviceIdentity identity;
    if (!unlocked.session().data.device_key_pem.empty() &&
        !unlocked.session().data.device_cert_pem.empty() &&
        !unlocked.session().data.device_fingerprint.empty()) {
        identity.private_key_pem = unlocked.session().data.device_key_pem;
        identity.certificate_pem = unlocked.session().data.device_cert_pem;
        identity.fingerprint = unlocked.session().data.device_fingerprint;
    } else {
        identity = generate_hrt_device_identity(device_name);
        unlocked.session().data.device_key_pem = identity.private_key_pem;
        unlocked.session().data.device_cert_pem = identity.certificate_pem;
        unlocked.session().data.device_fingerprint = identity.fingerprint;
        unlocked.session().data.updated_at = now_ms();
        const Paths paths = resolve_paths(parsed.options.config_dir);
        Vault::save(paths.vault_file, unlocked.session().data, unlocked.master());
    }

    /* Trust is decided before the pairing code is shown or sent. */
    const std::string trusted_pin =
        ensure_trust(unlocked, parsed, in, err, server);

    HttpOptions http = http_options_for(parsed, unlocked.session().data, err);
    HrtClientOptions options;
    options.proxy = http.proxy;
    options.expected_spki = trusted_pin;
    options.timeout_s = parsed.options.timeout_s;
    HrtClient client(server, options);
    client.connect();

    const std::string code = generate_hrt_code();
    const ToolVersion version = current_version();
    const std::string client_version = std::to_string(version.major) + "." +
                                       std::to_string(version.minor) + "." +
                                       std::to_string(version.patch);
    const HrtPairRequest request =
        start_hrt_pairing(client, username, device_name, identity.certificate_pem,
                          code, client_version);

    std::ostream &code_out = parsed.options.json ? err : out;
    code_out << "Verification code (128 hex):\n" << code << "\n";
    code_out << "Approve this device at:\n  " << request.verification_url << "\n";
    code_out << "Waiting for web approval...\n";
    code_out.flush();

    const auto now_seconds = [] {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    };
    HrtPairResult result;
    while (true) {
        result = poll_hrt_pairing(client, request);
        if (result.status == HrtPairStatus::Approved) {
            break;
        }
        if (result.status == HrtPairStatus::Denied) {
            client.close();
            save_login_failure(unlocked, parsed);
            throw ApiAuthError("PAIR_DENIED", "the login request was denied in the web app");
        }
        if (result.status == HrtPairStatus::Expired ||
            now_seconds() >= request.expires_at) {
            client.close();
            save_login_failure(unlocked, parsed);
            throw ApiAuthError("PAIR_EXPIRED",
                               "the login request expired; run hrt-tool login again");
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    client.close();

    /* The account password never goes to the server; it only derives the
       local cloud-data key. */
    const std::string password = read_account_password(parsed.options, in, out);
    const std::array<uint8_t, 32> key = derive_cloud_key(password, result.user_id);

    unlocked.session().data.last_login_failed_at = 0;
    unlocked.session().data.token = result.token;
    unlocked.session().data.user_id = result.user_id;
    unlocked.session().data.username = result.username;
    unlocked.session().data.device_pass.clear();
    unlocked.session().data.cloud_key =
        parsed.options.cache_cloud_key ? cloud_key_to_hex(key) : "";
    unlocked.session().data.server = server;
    if (!parsed.options.proxy.empty()) {
        unlocked.session().data.proxy = parsed.options.proxy;
    }
    unlocked.session().data.opsec_ack = true;
    unlocked.session().data.updated_at = now_ms();
    const Paths paths = resolve_paths(parsed.options.config_dir);
    Vault::save(paths.vault_file, unlocked.session().data, unlocked.master());

    if (parsed.options.json) {
        out << nlohmann::json{
                   {"user",
                    {{"id", result.user_id}, {"username", result.username}}},
                   {"transport", "hrt/1"},
                   {"device",
                    {{"fingerprint", identity.fingerprint},
                     {"name", device_name}}}}
                   .dump(2)
            << "\n";
    } else {
        out << "logged in as " << result.username << " (" << result.user_id
            << ") via hrt/1\n";
        out << "device: " << device_name << " [" << identity.fingerprint << "]\n";
    }
    return 0;
}

int command_login(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                  std::ostream &err, SessionContext *context) {
    UnlockedVault unlocked = unlock_full(parsed.options, in, out, context);
    enforce_login_cooldown(unlocked.session().data);
    const std::string server = server_for(parsed, unlocked.session().data);
    if (parsed.options.require_hrt && !is_hrt_url(server)) {
        throw ToolError(ExitCode::Usage, "HRT_REQUIRED",
                        "--require-hrt was given but the server is not hrt://; "
                        "run hrt-tool config set server hrt://HOST[:PORT]");
    }
    const std::string username = read_username(parsed, in, out);
    if (is_hrt_url(server)) {
        return login_hrt(unlocked, parsed, username, server, in, out, err);
    }
    err << "warning: HTTPS terminates at the server (and any CDN in front of "
           "it); this transport is not end-to-end encrypted. Use an hrt:// "
           "server when the deployment offers one.\n";
    /* Decide trust before the account password is typed or sent. */
    const std::string trusted_pin =
        ensure_trust(unlocked, parsed, in, err, server);
    const std::string password = read_account_password(parsed.options, in, out);
    /* A device pass is a credential for one server; never send a cached one
       to a different origin. */
    const std::string device_pass =
        server == unlocked.session().data.server ? unlocked.session().data.device_pass
                                               : std::string{};

    HttpOptions http = http_options_for(parsed, unlocked.session().data, err);
    http.pinned_spki_sha256 = trusted_pin;
    std::unique_ptr<ApiClient> client = make_api_client(server, http);
    LoginResult result;
    try {
        try {
            result = client->login(username, password, "", "", device_pass);
        } catch (const ApiTwoFactorRequired &required) {
            const bool passkey = required.method() == "passkey";
            const std::string code = read_password(
                in, out, passkey ? "Backup code: " : "2FA code (or backup code): ",
                false, true);
            const bool numeric =
                code.size() == 6 &&
                std::all_of(code.begin(), code.end(),
                            [](char c) { return c >= '0' && c <= '9'; });
            if (!passkey && numeric) {
                result = client->login(username, password, code, "", device_pass);
            } else {
                result = client->login(username, password, "", code, device_pass);
            }
        }
    } catch (const ApiAuthError &) {
        save_login_failure(unlocked, parsed);
        throw;
    }

    unlocked.session().data.last_login_failed_at = 0;
    const std::array<uint8_t, 32> key =
        derive_cloud_key(password, result.user_id);
    if (!unlocked.session().data.server.empty() &&
        !same_origin(server, unlocked.session().data.server)) {
        /* Credentials belong to the old origin; never carry them over. */
        unlocked.session().data.token.clear();
        unlocked.session().data.cloud_key.clear();
        unlocked.session().data.device_pass.clear();
    }
    unlocked.session().data.token = result.token;
    unlocked.session().data.user_id = result.user_id;
    unlocked.session().data.username = result.username;
    unlocked.session().data.device_pass = result.device_pass;
    unlocked.session().data.cloud_key =
        parsed.options.cache_cloud_key ? cloud_key_to_hex(key) : "";
    unlocked.session().data.server = server;
    if (!parsed.options.proxy.empty()) {
        unlocked.session().data.proxy = parsed.options.proxy;
    }
    unlocked.session().data.opsec_ack = true;
    unlocked.session().data.updated_at = now_ms();
    const Paths paths = resolve_paths(parsed.options.config_dir);
    Vault::save(paths.vault_file, unlocked.session().data, unlocked.master());

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
                   std::ostream &err, SessionContext *context) {
    UnlockedVault unlocked = unlock_full(parsed.options, in, out, context);
    const bool had_token = !unlocked.session().data.token.empty();
    const bool had_local =
        had_token || !unlocked.session().data.cloud_key.empty() ||
        !unlocked.session().data.device_pass.empty();
    if (!had_local) {
        out << "not logged in\n";
        return 0;
    }

    std::string warning;
    if (had_token) {
        const std::string saved = unlocked.session().data.server.empty()
                                      ? parsed.options.server
                                      : unlocked.session().data.server;
        try {
            const std::string trusted_pin =
                ensure_trust(unlocked, parsed, in, err, saved);
            HttpOptions http =
                http_options_for(parsed, unlocked.session().data, err);
            http.pinned_spki_sha256 = trusted_pin;
            std::unique_ptr<ApiClient> client =
                make_api_client(saved, http, unlocked.session().data.token);
            client->logout();
        } catch (const ToolError &error) {
            /* Revocation is best effort; local secrets are cleared either
               way so a network or trust failure cannot leave a usable token
               on disk. */
            warning = error.what();
        }
    }

    unlocked.session().data.token.clear();
    unlocked.session().data.cloud_key.clear();
    unlocked.session().data.device_pass.clear();
    unlocked.session().data.updated_at = now_ms();
    const Paths paths = resolve_paths(parsed.options.config_dir);
    Vault::save(paths.vault_file, unlocked.session().data, unlocked.master());
    if (parsed.options.json) {
        out << "{\"ok\":true}\n";
    } else {
        out << (had_token ? "logged out\n" : "local credentials cleared\n");
        if (!unlocked.session().data.device_cert_pem.empty()) {
            out << "the per-install device identity was kept; the next hrt:// "
                   "login still needs web approval\n";
        }
    }
    if (!warning.empty()) {
        err << "warning: session revocation failed (" << warning
            << "); local credentials cleared\n";
    }
    return 0;
}

int command_whoami(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                   std::ostream &err, SessionContext *context) {
    UnlockedVault unlocked = unlock_full(parsed.options, in, out, context);
    if (unlocked.session().data.token.empty()) {
        throw ApiAuthError("NOT_LOGGED_IN", "log in first; run hrt-tool login");
    }
    const std::string server = authenticated_server(parsed, unlocked.session().data);
    const std::string trusted_pin =
        ensure_trust(unlocked, parsed, in, err, server);
    HttpOptions http = http_options_for(parsed, unlocked.session().data, err);
    http.pinned_spki_sha256 = trusted_pin;
    std::unique_ptr<ApiClient> client =
        make_api_client(server, http, unlocked.session().data.token);
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

std::optional<Snapshot> fetch_latest_snapshot(ApiClient &client,
                                              const ParsedArgs &parsed,
                                              std::ostream &err) {
    for (int attempt = 0;; ++attempt) {
        try {
            return client.latest_snapshot();
        } catch (const NetworkFailure &failure) {
            const bool debug = parsed.options.debug || parsed.options.trace;
            if (!should_retry(failure.kind(), true, debug, attempt)) {
                if (failure.kind() == FailureKind::TunnelInterrupted &&
                    attempt >= 2) {
                    throw NetworkFailure(failure.code(), failure.what(),
                                         FailureKind::FinalFallback);
                }
                throw;
            }
            err << failure_human_line(Language::En, FailureKind::Timeout) << "\n";
            std::this_thread::sleep_for(
                std::chrono::milliseconds(attempt == 0 ? 500 : 1500));
        }
    }
}

int command_look_remote(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                        std::ostream &err, SessionContext *context) {
    UnlockedVault unlocked = unlock_full(parsed.options, in, out, context);
    if (unlocked.session().data.token.empty()) {
        throw ApiAuthError("NOT_LOGGED_IN", "log in first; run hrt-tool login");
    }
    const std::string server = authenticated_server(parsed, unlocked.session().data);
    const std::string trusted_pin =
        ensure_trust(unlocked, parsed, in, err, server);
    HttpOptions http = http_options_for(parsed, unlocked.session().data, err);
    http.pinned_spki_sha256 = trusted_pin;
    std::unique_ptr<ApiClient> client =
        make_api_client(server, http, unlocked.session().data.token);
    std::optional<Snapshot> snapshot = fetch_latest_snapshot(*client, parsed, err);
    if (!snapshot.has_value()) {
        throw ToolError(ExitCode::Data, "NO_SNAPSHOT",
                        "the account has no snapshot yet");
    }
    const std::array<uint8_t, 32> key =
        cloud_key_for(unlocked, parsed, in, out);
    return handle_look_payload(parsed, payload_from_snapshot(*snapshot, key), out);
}

int command_status_remote(const ParsedArgs &parsed, std::istream &in,
                          std::ostream &out, std::ostream &err,
                          SessionContext *context) {
    UnlockedVault unlocked = unlock_full(parsed.options, in, out, context);
    if (unlocked.session().data.token.empty()) {
        throw ApiAuthError("NOT_LOGGED_IN", "log in first; run hrt-tool login");
    }

    ParsedArgs effective = parsed;
    if (effective.options.mode.empty()) {
        effective.options.mode = unlocked.session().data.mode;
    }
    const std::string server =
        authenticated_server(effective, unlocked.session().data);
    const std::string trusted_pin =
        ensure_trust(unlocked, effective, in, err, server);
    HttpOptions http =
        http_options_for(effective, unlocked.session().data, err);
    http.pinned_spki_sha256 = trusted_pin;
    std::unique_ptr<ApiClient> client =
        make_api_client(server, http, unlocked.session().data.token);
    const std::optional<Snapshot> snapshot =
        fetch_latest_snapshot(*client, effective, err);
    if (!snapshot.has_value()) {
        throw ToolError(ExitCode::Data, "NO_SNAPSHOT",
                        "the account has no snapshot yet");
    }
    const std::array<uint8_t, 32> key =
        cloud_key_for(unlocked, effective, in, out);
    const nlohmann::json payload =
        payload_from_snapshot(*snapshot, key);

    const bool all =
        std::find(effective.command_args.begin(), effective.command_args.end(),
                  "all") != effective.command_args.end();
    const CommandFlags flags =
        parse_command_flags(effective.command_args, {"--watch"}, {});
    int watch = 0;
    if (flags.values.count("--watch") != 0) {
        watch = parse_watch_seconds(flags.values.at("--watch"));
    }

    do {
        const nlohmann::json view =
            build_status_view(effective, payload, all, err);
        if (effective.options.json) {
            out << view.dump(2) << "\n";
        } else {
            render_status_table(view, all, out);
        }
        out.flush();
        if (watch <= 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(watch));
    } while (true);
    return 0;
}

nlohmann::json merge_for_push(const nlohmann::json &remote,
                              const nlohmann::json &local,
                              std::ostream &err) {
    MergeResult merge = merge_sync_payloads(remote, local);
    if (merge.aborted) {
        std::string message = "merge aborted:";
        for (const std::string &warning : merge.warnings) {
            message += " " + warning;
        }
        throw ToolError(ExitCode::Data, "MERGE_ABORTED", message);
    }
    for (const std::string &warning : merge.warnings) {
        err << "warning: " << warning << "\n";
    }
    return merge.payload;
}

struct PushResult {
    std::string id;
    int64_t created_at = 0;
    bool up_to_date = false;
};

/* Merge `local` with the newest server revision and write it, retrying the
 * optimistic loop on 409. `current` is the revision the caller already read,
 * if any; a stale or missing one is handled by the conflict path. */
PushResult push_sync_payload(ApiClient &client, const std::array<uint8_t, 32> &key,
                             const nlohmann::json &local, std::ostream &err,
                             std::optional<Snapshot> current) {
    nlohmann::json merged = local;
    std::string base;
    if (current.has_value()) {
        const nlohmann::json remote_payload =
            payload_from_snapshot(*current, key);
        if (fingerprint_state(normalize_sync_state(remote_payload)) ==
            fingerprint_state(normalize_sync_state(local))) {
            return {current->id, current->created_at, true};
        }
        merged = merge_for_push(remote_payload, local, err);
        if (fingerprint_state(normalize_sync_state(merged)) ==
            fingerprint_state(normalize_sync_state(remote_payload))) {
            return {current->id, current->created_at, true};
        }
        base = current->id;
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        try {
            const Snapshot pushed =
                client.put_snapshot(encrypt_cloud_bundle(merged, key), base);
            return {pushed.id, pushed.created_at, false};
        } catch (const SnapshotConflictError &conflict) {
            if (attempt == 2) {
                throw ToolError(ExitCode::Data, "CONFLICT",
                                "snapshot kept changing; try again");
            }
            std::optional<Snapshot> remote =
                conflict.current_id().empty()
                    ? client.latest_snapshot()
                    : std::optional<Snapshot>(
                          client.snapshot_by_id(conflict.current_id()));
            if (!remote.has_value()) {
                base.clear();
                merged = local;
                continue;
            }
            const nlohmann::json remote_payload =
                payload_from_snapshot(*remote, key);
            merged = merge_for_push(remote_payload, local, err);
            if (fingerprint_state(normalize_sync_state(merged)) ==
                fingerprint_state(normalize_sync_state(remote_payload))) {
                return {remote->id, remote->created_at, true};
            }
            base = remote->id;
        }
    }
    throw ToolError(ExitCode::Data, "CONFLICT", "snapshot push did not converge");
}

/* Live mode for add/dose/lab/weight: pull the newest revision, apply the same
 * payload mutation the local commands use, and push it back through the
 * optimistic merge loop. Read-only subcommands just print the pulled data. */
int command_mutate_remote(const ParsedArgs &parsed, std::istream &in,
                          std::ostream &out, std::ostream &err,
                          SessionContext *context) {
    UnlockedVault unlocked = unlock_full(parsed.options, in, out, context);
    if (unlocked.session().data.token.empty()) {
        throw ApiAuthError("NOT_LOGGED_IN", "log in first; run hrt-tool login");
    }
    const auto progress = [context](const std::string &phase) {
        if (context != nullptr && context->progress) {
            context->progress(phase);
        }
    };
    progress("trust");
    const std::string server = authenticated_server(parsed, unlocked.session().data);
    const std::string trusted_pin =
        ensure_trust(unlocked, parsed, in, err, server);
    HttpOptions http = http_options_for(parsed, unlocked.session().data, err);
    http.pinned_spki_sha256 = trusted_pin;
    std::unique_ptr<ApiClient> client =
        make_api_client(server, http, unlocked.session().data.token);

    const std::array<uint8_t, 32> key =
        cloud_key_for(unlocked, parsed, in, out);
    progress("fetching snapshot");
    std::optional<Snapshot> current = client->latest_snapshot();
    const nlohmann::json remote =
        current.has_value() ? payload_from_snapshot(*current, key)
                            : nlohmann::json::object();

    std::ostringstream captured;
    std::optional<nlohmann::json> updated;
    progress("applying");
    if (parsed.command == "add") {
        updated = apply_add(parsed, remote, captured, err);
    } else if (parsed.command == "dose") {
        updated = apply_dose(parsed, remote, captured);
    } else if (parsed.command == "lab") {
        updated = apply_lab(parsed, remote, captured);
    } else if (parsed.command == "weight") {
        updated = apply_weight(parsed, remote, captured);
    } else {
        throw ToolError(ExitCode::Usage, "USAGE",
                        "this command has no live mode");
    }
    if (!updated.has_value()) {
        out << captured.str();
        return 0;
    }

    /* Print the mutation line only after the write succeeds, so a failed push
       never looks like a saved change. */
    progress("uploading");
    const PushResult pushed =
        push_sync_payload(*client, key, *updated, err, current);
    out << captured.str();
    if (pushed.up_to_date) {
        out << "already up to date (" << pushed.id << ")\n";
    } else {
        out << "pushed " << pushed.id << " (" << pushed.created_at << ")\n";
    }
    return 0;
}

int command_snapshot(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                     std::ostream &err, SessionContext *context) {
    if (parsed.command_args.empty()) {
        throw ToolError(ExitCode::Usage, "USAGE",
                        "snapshot needs pull, push, or list");
    }
    const std::string sub = parsed.command_args.front();
    UnlockedVault unlocked = unlock_full(parsed.options, in, out, context);
    if (unlocked.session().data.token.empty()) {
        throw ApiAuthError("NOT_LOGGED_IN", "log in first; run hrt-tool login");
    }
    const std::string server = authenticated_server(parsed, unlocked.session().data);
    const std::string trusted_pin =
        ensure_trust(unlocked, parsed, in, err, server);
    HttpOptions http = http_options_for(parsed, unlocked.session().data, err);
    http.pinned_spki_sha256 = trusted_pin;
    std::unique_ptr<ApiClient> client =
        make_api_client(server, http, unlocked.session().data.token);

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
            if (!is_cloud_bundle(snapshot->data)) {
                /* Spec §8: legacy plaintext is stdout-only; never write a
                   plaintext payload to a file or wrap one the loader would
                   later reject (arrays, scalars) as a cloud bundle. */
                throw ToolError(ExitCode::Data, "SNAPSHOT_PLAINTEXT",
                                "the server returned a legacy plaintext snapshot; "
                                "use --out - to print it");
            }
            std::error_code exists_error;
            const bool exists = std::filesystem::exists(out_path, exists_error);
            if (exists_error) {
                throw ToolError(ExitCode::Vault, "SNAPSHOT_STAT",
                                "cannot stat " + out_path + ": " +
                                    exists_error.message());
            }
            if (exists && !parsed.options.force) {
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
        nlohmann::json local = read_sync_input(in_path, unlocked.master(), key);
        if (local.is_object() && local.contains("format") &&
            local.at("format").is_string() &&
            local.at("format").get<std::string>() == "oyama-hrt-complete-backup") {
            EnvelopeImport envelope = import_complete_envelope(local);
            local = envelope.sync_payload;
            for (const std::string &category : envelope.not_carried) {
                err << "not carried by the cloud sync payload: " << category << "\n";
            }
        }
        if (!local.is_object()) {
            throw ToolError(ExitCode::Data, "SNAPSHOT_INPUT",
                            "the input snapshot is not a sync payload object");
        }

        const PushResult pushed = push_sync_payload(
            *client, key, local, err, client->latest_snapshot());
        if (pushed.up_to_date) {
            out << "already up to date (" << pushed.id << ")\n";
        } else {
            out << "pushed " << pushed.id << " (" << pushed.created_at << ")\n";
        }
        return 0;
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

int command_config(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                   std::ostream &err, SessionContext *context) {
    if (parsed.command_args.empty()) {
        throw ToolError(ExitCode::Usage, "USAGE",
                        "config needs get, set, unset, or path");
    }
    const std::string sub = parsed.command_args.front();
    UnlockedVault unlocked = unlock_full(parsed.options, in, out, context);

    if (sub == "get") {
        const std::string key =
            parsed.command_args.size() > 1 ? parsed.command_args[1] : "";
        if (key.empty() || key == "server") {
            out << "server: " << unlocked.session().data.server << "\n";
        }
        if (key.empty() || key == "proxy") {
            out << "proxy: "
                << (unlocked.session().data.proxy.empty() ? "(none)"
                                                        : unlocked.session().data.proxy)
                << "\n";
        }
        if (key.empty() || key == "transport") {
            const bool raw = is_hrt_url(unlocked.session().data.server);
            out << "transport: " << (raw ? "hrt/1" : "tls") << "\n";
        }
        if (key.empty() || key == "compute") {
            out << "compute: local\n";
        }
        if (key.empty() || key == "language") {
            out << "language: " << unlocked.session().data.language << "\n";
        }
        if (key.empty() || key == "welcome") {
            out << "welcome: "
                << (unlocked.session().data.welcome.empty()
                        ? "(default)"
                        : unlocked.session().data.welcome)
                << "\n";
        }
        if (key.empty() || key == "humor") {
            out << "humor: " << (unlocked.session().data.humor ? "on" : "off") << "\n";
        }
        if (key.empty() || key == "mode") {
            out << "mode: "
                << (unlocked.session().data.mode.empty() ? "(default)"
                                                       : unlocked.session().data.mode)
                << "\n";
        }
        if (key.empty() || key == "active-link") {
            out << "active-link: "
                << (unlocked.session().data.active_link.empty()
                        ? "(base)"
                        : unlocked.session().data.active_link)
                << "\n";
        }
        if (!key.empty() && key != "server" && key != "proxy" &&
            key != "transport" && key != "compute" && key != "language" &&
            key != "welcome" && key != "humor" && key != "mode" &&
            key != "active-link") {
            throw ToolError(ExitCode::Usage, "CONFIG_KEY", "unknown config key: " + key);
        }
        return 0;
    }

    if (sub == "set" || sub == "unset") {
        const std::string key =
            parsed.command_args.size() > 1 ? parsed.command_args[1] : "";
        const std::string value =
            parsed.command_args.size() > 2 ? parsed.command_args[2] : "";
        if (key.empty()) {
            throw ToolError(ExitCode::Usage, "CONFIG_KEY", "missing config key");
        }
        if (sub == "set" && value.empty()) {
            throw ToolError(ExitCode::Usage, "CONFIG_VALUE", "missing value");
        }
        if (key == "language") {
            if (sub == "unset") {
                unlocked.session().data.language = "en";
                out << "language set: en\n";
            } else if (value == "en" || value == "ru") {
                unlocked.session().data.language = value;
                out << "language set: " << value << "\n";
            } else {
                throw ToolError(ExitCode::Config, "CONFIG_VALUE",
                                "language must be en or ru");
            }
        } else if (key == "welcome") {
            if (sub == "unset") {
                unlocked.session().data.welcome.clear();
                out << "welcome unset\n";
            } else {
                unlocked.session().data.welcome = value;
                out << "welcome set\n";
            }
        } else if (key == "humor") {
            if (sub == "unset") {
                unlocked.session().data.humor = true;
                out << "humor set: on\n";
            } else if (value == "on" || value == "true") {
                unlocked.session().data.humor = true;
                out << "humor set: on\n";
            } else if (value == "off" || value == "false") {
                unlocked.session().data.humor = false;
                out << "humor set: off\n";
            } else {
                throw ToolError(ExitCode::Config, "CONFIG_VALUE",
                                "humor must be on or off");
            }
        } else if (key == "mode") {
            if (sub == "unset") {
                unlocked.session().data.mode.clear();
                out << "mode unset\n";
            } else if (value == "transfem" || value == "transmasc") {
                unlocked.session().data.mode = value;
                out << "mode set: " << value << "\n";
            } else {
                throw ToolError(ExitCode::Config, "CONFIG_VALUE",
                                "mode must be transfem or transmasc");
            }
        } else if (key == "active-link") {
            const std::string name =
                sub == "unset" || value == "-" ? std::string{} : value;
            select_link(unlocked.session().data, name);
            out << "active link: " << (name.empty() ? "(base)" : name) << "\n";
        } else if (key == "proxy") {
            if (sub == "unset") {
                unlocked.session().data.proxy.clear();
                out << "proxy unset\n";
            } else {
                const std::string normalized = normalize_proxy_url(value, &out);
                validate_proxy_url(normalized);
                unlocked.session().data.proxy = normalized;
                out << "proxy set: " << normalized << "\n";
            }
        } else if (key == "server") {
            const std::string next =
                sub == "unset" ? "https://hrt.mahiro.uk" : value;
            const Url next_url = parse_url(next);
            if (next_url.scheme == "hrt") {
                if (!next_url.path.empty() && next_url.path != "/") {
                    throw ToolError(ExitCode::Usage, "HRT_URL",
                                    "an hrt:// server URL has no path");
                }
                validate_hidden_service_host(next_url);
            } else {
                validate_server_url(next_url);
                validate_hidden_service_host(next_url);
            }
            if (!unlocked.session().data.server.empty() &&
                !same_origin(next, unlocked.session().data.server)) {
                unlocked.session().data.token.clear();
                unlocked.session().data.cloud_key.clear();
                unlocked.session().data.device_pass.clear();
                unlocked.session().data.device_key_pem.clear();
                unlocked.session().data.device_cert_pem.clear();
                unlocked.session().data.device_fingerprint.clear();
                err << "warning: server changed; the saved session and device "
                       "identity were cleared\n";
            }
            unlocked.session().data.server = next;
            out << "server set: " << next << "\n";
        } else if (key == "transport") {
            throw ToolError(ExitCode::Usage, "TRANSPORT_SELECTION",
                            "the transport follows the server URL: use "
                            "config set server hrt://HOST[:PORT] for hrt/1, "
                            "or an https:// URL for TLS");
        } else if (key == "compute") {
            throw ToolError(ExitCode::NotImplemented, "NOT_IMPLEMENTED",
                            "server compute is reserved; computation is local");
        } else {
            throw ToolError(ExitCode::Usage, "CONFIG_KEY", "unknown config key: " + key);
        }
        unlocked.session().data.updated_at = now_ms();
        const Paths paths = resolve_paths(parsed.options.config_dir);
        Vault::save(paths.vault_file, unlocked.session().data, unlocked.master());
        return 0;
    }

    throw ToolError(ExitCode::Usage, "USAGE",
                    "config needs get, set, unset, or path");
}

int command_config_path(const ParsedArgs &parsed, std::ostream &out) {
    const Paths paths = resolve_paths(parsed.options.config_dir);
    out << "config_dir: " << paths.config_dir.string() << "\n";
    out << "vault: " << paths.vault_file.string() << "\n";
    return 0;
}

int command_logs_show(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                      SessionContext *context) {
    if (parsed.options.log_file.empty()) {
        throw ToolError(ExitCode::Usage, "USAGE", "logs show requires --log-file FILE");
    }
    UnlockedVault unlocked = unlock_required(parsed.options, in, out, context);
    EncryptedLog::show(parsed.options.log_file, unlocked.session().log_key, out);
    return 0;
}

int command_stub(const std::string &name, const ParsedArgs &parsed, std::istream &in,
                 std::ostream &out, std::ostream &err, SessionContext *context) {
    UnlockedVault unlocked = unlock_required(parsed.options, in, out, context);
    if (parsed.options.debug) {
        err << "debug: " << name << " (M1 stub)\n";
    }
    if (!parsed.options.log_file.empty() &&
        (parsed.options.debug || parsed.options.trace)) {
        EncryptedLog::append(parsed.options.log_file, unlocked.session().log_key,
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
        (void)client->login("", "", "", "", unlocked.session().data.device_pass);
    } else if (name == "logout") {
        client->logout();
    } else if (name == "whoami") {
        (void)client->me();
    }

    throw ToolError(ExitCode::NotImplemented, "NOT_IMPLEMENTED",
                    name + " is not implemented in M1");
}

int command_add_link(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                     std::ostream &err, SessionContext *context) {
    if (parsed.command_args.size() != 2) {
        throw ToolError(ExitCode::Usage, "USAGE",
                        "add-link needs URL NAME");
    }
    const std::string &url = parsed.command_args[0];
    const std::string &name = parsed.command_args[1];
    validate_server_target(url);

    LinkData link;
    link.url = url;
    link.mode = parsed.options.mode;
    if (parsed.options.proxy_explicit) {
        link.proxy = normalize_proxy_url(parsed.options.proxy, &err);
    }

    UnlockedVault unlocked = unlock_full(parsed.options, in, out, context);
    if (parsed.options.probe) {
        ParsedArgs probe = parsed;
        probe.options.server = url;
        probe.options.server_explicit = true;
        probe.options.proxy = link.proxy;
        probe.options.proxy_explicit = true;
        const std::string pin = ensure_trust(unlocked, probe, in, err, url);
        if (!pin.empty()) {
            err << "pinned: " << pin << "\n";
        }
    }

    add_link(unlocked.session().data, name, link, parsed.options.force);
    unlocked.session().data.updated_at = now_ms();
    const Paths paths = resolve_paths(parsed.options.config_dir);
    Vault::save(paths.vault_file, unlocked.session().data, unlocked.master());
    out << "link added: " << name << "\n";
    return 0;
}

int command_commit_server(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                          SessionContext *context) {
    if (parsed.command_args.size() != 1) {
        throw ToolError(ExitCode::Usage, "USAGE",
                        "commit-server needs a link name (use - for the base link)");
    }
    const std::string name =
        parsed.command_args[0] == "-" ? std::string{} : parsed.command_args[0];
    UnlockedVault unlocked = unlock_full(parsed.options, in, out, context);
    select_link(unlocked.session().data, name);
    const Paths paths = resolve_paths(parsed.options.config_dir);
    Vault::save(paths.vault_file, unlocked.session().data, unlocked.master());
    out << "active link: " << (name.empty() ? "(base)" : name) << "\n";
    return 0;
}

int command_link(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                 std::ostream &err, SessionContext *context) {
    if (parsed.command_args.empty()) {
        throw ToolError(ExitCode::Usage, "USAGE",
                        "link needs list, show, rename, or remove");
    }
    const std::string sub = parsed.command_args.front();
    UnlockedVault unlocked = unlock_full(parsed.options, in, out, context);
    VaultData &data = unlocked.session().data;

    if (sub == "list") {
        for (const auto &entry : data.links) {
            const std::string &name = entry.first;
            const LinkData &link = entry.second;
            out << (name.empty() ? "(base)" : name) << "  " << link.url
                << "  mode=" << (link.mode.empty() ? "default" : link.mode)
                << "  " << (link.token.empty() ? "logged-out" : "logged-in")
                << (name == data.active_link ? "  active" : "") << "\n";
        }
        return 0;
    }
    if (sub == "show") {
        if (parsed.command_args.size() != 2) {
            throw ToolError(ExitCode::Usage, "USAGE", "link show needs a name");
        }
        const std::string &name = parsed.command_args[1];
        out << link_summary(name, require_link(data, name));
        return 0;
    }
    if (sub == "rename") {
        if (parsed.command_args.size() != 3) {
            throw ToolError(ExitCode::Usage, "USAGE", "link rename needs OLD NEW");
        }
        rename_link(data, parsed.command_args[1], parsed.command_args[2]);
        data.updated_at = now_ms();
        const Paths paths = resolve_paths(parsed.options.config_dir);
        Vault::save(paths.vault_file, data, unlocked.master());
        out << "link renamed: " << parsed.command_args[1] << " -> "
            << parsed.command_args[2] << "\n";
        return 0;
    }
    if (sub == "remove") {
        if (parsed.command_args.size() != 2) {
            throw ToolError(ExitCode::Usage, "USAGE", "link remove needs a name");
        }
        const std::string &name = parsed.command_args[1];
        (void)err;
        if (!parsed.options.yes) {
            out << "Remove link " << name << "? [y/N]: ";
            out.flush();
            std::string answer;
            if (!std::getline(in, answer)) {
                throw ToolError(ExitCode::Cancelled, "CANCELLED",
                                "link removal cancelled");
            }
            if (!answer.empty() && answer.back() == '\r') {
                answer.pop_back();
            }
            if (answer != "y" && answer != "yes") {
                throw ToolError(ExitCode::Cancelled, "CANCELLED",
                                "link removal cancelled");
            }
        }
        remove_link(data, name, parsed.options.force);
        data.updated_at = now_ms();
        const Paths paths = resolve_paths(parsed.options.config_dir);
        Vault::save(paths.vault_file, data, unlocked.master());
        out << "link removed: " << name << "\n";
        return 0;
    }
    throw ToolError(ExitCode::Usage, "USAGE",
                    "link needs list, show, rename, or remove");
}

const std::set<std::string> &known_commands() {
    static const std::set<std::string> kCommands = {
        "version", "init",    "login",    "logout",    "whoami", "look",
        "add",     "dose",    "lab",      "weight",    "export", "import",
        "snapshot", "config", "logs",     "add-link",  "commit-server", "link",
        "console",  "status", "proxy",    "doctor",    "completion",    "help",
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
             std::ostream &err, SessionContext *context) {
    (void)err;

    if (!known_commands().count(parsed.command)) {
        throw ToolError(ExitCode::Usage, "USAGE", "unknown command: " + parsed.command);
    }
    if (parsed.options.transport == "openssl-v1") {
        throw ToolError(ExitCode::NotImplemented, "NOT_IMPLEMENTED",
                        "the openssl-v1 transport envelope is not implemented yet");
    }
    if (parsed.options.compute == "server") {
        throw ToolError(ExitCode::NotImplemented, "NOT_IMPLEMENTED",
                        "server compute is reserved; use --compute local");
    }

    if (parsed.command == "version") {
        return command_version(parsed, out);
    }
    if (parsed.command == "help") {
        return command_help(parsed, out);
    }
    if (parsed.command == "completion") {
        return command_completion(parsed, out);
    }
    if (parsed.command == "proxy") {
        return command_proxy(parsed, in, out, err, context);
    }
    if (parsed.command == "doctor") {
        return command_doctor(parsed, in, out, err, context);
    }
    if (parsed.command == "console") {
        return run_console(parsed.options, in, out, err);
    }
    if (parsed.command == "init") {
        return command_init(parsed, in, out, err);
    }
    if (parsed.command == "login") {
        return command_login(parsed, in, out, err, context);
    }
    if (parsed.command == "logout") {
        return command_logout(parsed, in, out, err, context);
    }
    if (parsed.command == "whoami") {
        return command_whoami(parsed, in, out, err, context);
    }
    if (parsed.command == "add-link") {
        return command_add_link(parsed, in, out, err, context);
    }
    if (parsed.command == "commit-server") {
        return command_commit_server(parsed, in, out, context);
    }
    if (parsed.command == "link") {
        return command_link(parsed, in, out, err, context);
    }
    if (parsed.command == "snapshot") {
        return command_snapshot(parsed, in, out, err, context);
    }
    if (parsed.command == "config") {
        if (!parsed.command_args.empty() && parsed.command_args.front() == "path") {
            return command_config_path(parsed, out);
        }
        return command_config(parsed, in, out, err, context);
    }
    if (parsed.command == "logs") {
        if (!parsed.command_args.empty() && parsed.command_args.front() == "show") {
            return command_logs_show(parsed, in, out, context);
        }
        const std::string name =
            parsed.command_args.empty() ? "logs" : "logs " + parsed.command_args.front();
        return command_stub(name, parsed, in, out, err, context);
    }

    if (parsed.command == "status") {
        if (parsed.options.snapshot.empty()) {
            const Paths paths = resolve_paths(parsed.options.config_dir);
            if (Vault::exists(paths.vault_file)) {
                return command_status_remote(parsed, in, out, err, context);
            }
        }
        return handle_status_local(parsed, in, out, err);
    }

    if (parsed.command == "look" && parsed.options.snapshot.empty()) {
        const Paths paths = resolve_paths(parsed.options.config_dir);
        if (Vault::exists(paths.vault_file)) {
            return command_look_remote(parsed, in, out, err, context);
        }
    }

    const bool interactive_dose =
        parsed.command == "add" && has_argument(parsed.command_args, "dose") &&
        !has_argument(parsed.command_args, "--drug") &&
        !has_argument(parsed.command_args, "--time") &&
        !has_argument(parsed.command_args, "--measurement") &&
        !has_argument(parsed.command_args, "--dose") &&
        !has_argument(parsed.command_args, "--route");
    if (interactive_dose) {
        return handle_add_interactive(parsed, in, out, err, context);
    }

    const bool live_mutation = parsed.command == "add" ||
                               parsed.command == "dose" ||
                               parsed.command == "lab" ||
                               parsed.command == "weight";
    if (live_mutation && parsed.options.snapshot.empty()) {
        const Paths paths = resolve_paths(parsed.options.config_dir);
        if (Vault::exists(paths.vault_file)) {
            return command_mutate_remote(parsed, in, out, err, context);
        }
    }

    if (is_local_command(parsed.command)) {
        return handle_local_command(parsed, in, out, err);
    }

    std::string name = parsed.command;
    if (!parsed.command_args.empty()) {
        name += " " + parsed.command_args.front();
    }
    return command_stub(name, parsed, in, out, err, context);
}
