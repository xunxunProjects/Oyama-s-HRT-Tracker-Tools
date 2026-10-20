#include "cli.h"

#include "commands.h"
#include "console.h"
#include "error.h"
#include "net_error.h"

#include <nlohmann/json.hpp>
#include <cstdio>
#include <string>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

bool stdin_is_tty() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return ::isatty(STDIN_FILENO) != 0;
#endif
}

} // namespace

namespace {

[[noreturn]] void usage_error(const std::string &message) {
    throw ToolError(ExitCode::Usage, "USAGE", message);
}

} // namespace

static ParsedArgs parse_args(int argc, const char *const *argv) {
    ParsedArgs parsed;
    bool command_seen = false;

    for (int i = 1; i < argc; ++i) {
        const std::string token = argv[i];

        /* f2f owns every token after the command: its flags overlap the
           global names and have different semantics. */
        if (command_seen && parsed.command == "f2f") {
            parsed.command_args.push_back(token);
            continue;
        }

        if (token == "--") {
            for (++i; i < argc; ++i) {
                if (!command_seen) {
                    parsed.command = argv[i];
                    command_seen = true;
                } else {
                    parsed.command_args.push_back(argv[i]);
                }
            }
            break;
        }

        if (token == "-h" || token == "--help") {
            parsed.help = true;
            continue;
        }

        if (!token.empty() && token[0] == '-') {
            std::string name = token;
            std::string inline_value;
            bool has_inline_value = false;
            const std::string::size_type equals = token.find('=');
            if (equals != std::string::npos) {
                name = token.substr(0, equals);
                inline_value = token.substr(equals + 1);
                has_inline_value = true;
            }

            const auto take_value = [&]() -> std::string {
                if (has_inline_value) {
                    return inline_value;
                }
                if (i + 1 >= argc) {
                    usage_error("missing value for " + name);
                }
                return argv[++i];
            };
            const auto boolean_flag = [&]() {
                if (has_inline_value) {
                    usage_error(name + " does not take a value");
                }
            };

            if (name == "--server") {
                parsed.options.server = take_value();
                parsed.options.server_explicit = true;
            } else if (name == "--proxy") {
                parsed.options.proxy = take_value();
                parsed.options.proxy_explicit = true;
            } else if (name == "--config-dir") {
                parsed.options.config_dir = take_value();
            } else if (name == "--token-file") {
                parsed.options.token_file = take_value();
            } else if (name == "--debug") {
                boolean_flag();
                parsed.options.debug = true;
            } else if (name == "--trace") {
                boolean_flag();
                parsed.options.trace = true;
            } else if (name == "--json") {
                boolean_flag();
                parsed.options.json = true;
            } else if (name == "--no-color") {
                boolean_flag();
                parsed.options.no_color = true;
            } else if (name == "--yes") {
                boolean_flag();
                parsed.options.yes = true;
            } else if (name == "--force") {
                boolean_flag();
                parsed.options.force = true;
            } else if (name == "--probe") {
                boolean_flag();
                parsed.options.probe = true;
            } else if (name == "--no-humor") {
                boolean_flag();
                parsed.options.no_humor = true;
            } else if (name == "--trust-server") {
                boolean_flag();
                parsed.options.trust_server = true;
            } else if (name == "--require-hrt") {
                boolean_flag();
                parsed.options.require_hrt = true;
            } else if (name == "--device-name") {
                parsed.options.device_name = take_value();
            } else if (name == "--insecure-tls") {
                boolean_flag();
                parsed.options.insecure_tls = true;
            } else if (name == "--no-cache-cloud-key") {
                boolean_flag();
                parsed.options.cache_cloud_key = false;
            } else if (name == "--vault-password-stdin") {
                boolean_flag();
                parsed.options.vault_password_stdin = true;
            } else if (name == "--timeout") {
                const std::string value = take_value();
                long seconds = 0;
                try {
                    seconds = std::stol(value);
                } catch (...) {
                    usage_error("--timeout must be a number of seconds");
                }
                if (seconds <= 0 || seconds > 3600) {
                    usage_error("--timeout must be between 1 and 3600 seconds");
                }
                parsed.options.timeout_s = seconds;
            } else if (name == "--transport") {
                const std::string value = take_value();
                if (value != "auto" && value != "tls" && value != "openssl-v1") {
                    usage_error("--transport must be auto, tls, or openssl-v1");
                }
                parsed.options.transport = value;
            } else if (name == "--compute") {
                const std::string value = take_value();
                if (value != "local" && value != "server") {
                    usage_error("--compute must be local or server");
                }
                parsed.options.compute = value;
            } else if (name == "--vault-password-file") {
                parsed.options.vault_password_file = take_value();
            } else if (name == "--log-file") {
                parsed.options.log_file = take_value();
            } else if (name == "--user-id") {
                parsed.options.user_id = take_value();
            } else if (name == "--account-password-file") {
                parsed.options.account_password_file = take_value();
            } else if (name == "--account-password-stdin") {
                boolean_flag();
                parsed.options.account_password_stdin = true;
            } else if (name == "--snapshot") {
                parsed.options.snapshot = take_value();
            } else if (name == "--mode") {
                const std::string value = take_value();
                if (value != "transfem" && value != "transmasc") {
                    usage_error("--mode must be transfem or transmasc");
                }
                parsed.options.mode = value;
            } else if (name == "--utc") {
                boolean_flag();
                parsed.options.utc = true;
            } else {
                if (!command_seen) {
                    usage_error("unknown option: " + name);
                }
                parsed.command_args.push_back(token);
            }
            continue;
        }

        if (!command_seen) {
            parsed.command = token;
            command_seen = true;
        } else {
            parsed.command_args.push_back(token);
        }
    }

    return parsed;
}

ParsedArgs parse_cli_args(int argc, const char *const *argv) {
    return parse_args(argc, argv);
}

void print_help(std::ostream &out) {
    out << "hrt-tool — Oyama HRT Tracker terminal client\n\n"
        << "Usage: hrt-tool [global flags] <command> [command flags]\n\n"
        << "Commands:\n"
        << "  init                 create the encrypted local vault\n"
        << "  login                log in and store an encrypted session\n"
        << "  logout               revoke the session and forget the token\n"
        << "  whoami               show the current account\n"
        << "  snapshot pull        download an encrypted snapshot\n"
        << "  snapshot push        merge and upload a snapshot\n"
        << "  snapshot list        list stored revisions\n"
        << "  look                 show concentrations (remote, or from --snapshot)\n"
        << "  add                  add a dose\n"
        << "  dose ...             manage doses\n"
        << "  lab ...              manage labs\n"
        << "  weight ...           manage weight\n"
        << "  export               export data\n"
        << "  import               import data\n"
        << "  config get [KEY]     show server, proxy, transport, compute\n"
        << "  config set KEY VALUE change server or proxy\n"
        << "  config unset KEY     clear server or proxy\n"
        << "  config path          print config paths\n"
        << "  logs show            decrypt an encrypted log file\n"
        << "  version              print versions\n\n"
        << "Global flags:\n"
        << "  --server URL  --proxy URL  --config-dir DIR  --debug  --trace\n"
        << "  --json  --no-color  --yes  --timeout SECONDS  --trust-server\n"
        << "  --require-hrt  --device-name NAME\n"
        << "  --insecure-tls  --transport auto|tls|openssl-v1\n"
        << "  --no-cache-cloud-key  --compute local|server\n"
        << "  --vault-password-file FILE  --vault-password-stdin  --log-file FILE\n"
        << "  --snapshot FILE  --mode transfem|transmasc  --utc\n"
        << "  --user-id ID  --account-password-file FILE  --account-password-stdin\n\n"
        << "Command flags:\n"
        << "  login --username USER  (a failed credential attempt pauses login 60s)\n"
        << "        hrt:// login: the CLI shows a 128-hex code; approve it in the\n"
        << "        web account page, then type the local account password.\n"
        << "  snapshot pull --out FILE [--id ID] [--force]\n"
        << "  snapshot push --in FILE\n"
        << "  look --all  --at OFFSETS  --labs N  --json\n\n"
        << "Trust:\n"
        << "  On first contact the certificate and SPKI SHA-256 are shown.\n"
        << "  Type 'yes' to trust and pin, or use --trust-server to persist the\n"
        << "  decision non-interactively. --yes trusts for the current run only.\n"
        << "  A changed pin is refused until --trust-server is given.\n\n"
        << "Tor/I2P:\n"
        << "  Tor: hrt-tool config set proxy socks5h://127.0.0.1:9050\n"
        << "  I2P: hrt-tool config set proxy socks5h://127.0.0.1:4447\n"
        << "  Only socks5h:// is accepted, so the proxy resolves every name;\n"
        << "  socks5, http, and https proxies are refused.\n"
        << "  .onion is v3: 56 base32 characters. I2P names end in .i2p;\n"
        << "  a destination hash is 52 base32 characters plus .b32.i2p.\n\n"
        << "Plaintext is written only to stdout. All local state is encrypted at rest.\n";
}

int run_cli(int argc, const char *const *argv, std::istream &in, std::ostream &out,
            std::ostream &err) {
    ParsedArgs parsed;
    try {
        if (argc == 1 && stdin_is_tty()) {
            return run_console(GlobalOptions{}, in, out, err);
        }
        parsed = parse_args(argc, argv);
        if (parsed.help || parsed.command.empty()) {
            print_help(out);
            return 0;
        }
        return dispatch(parsed, in, out, err);
    } catch (const NetworkFailure &error) {
        if (should_print_human_line(parsed.options.debug, parsed.options.trace,
                                    parsed.options.no_humor, true)) {
            const std::string human =
                failure_human_line(Language::En, error.kind());
            if (!human.empty()) {
                err << human << "\n";
            }
        }
        err << "hrt-tool: " << error.what() << " [" << error.code() << "]\n";
        return static_cast<int>(error.exit_code());
    } catch (const ToolError &error) {
        err << "hrt-tool: " << error.what() << " [" << error.code() << "]\n";
        return static_cast<int>(error.exit_code());
    } catch (const nlohmann::json::exception &error) {
        err << "hrt-tool: malformed data: " << error.what() << " [DATA]\n";
        return static_cast<int>(ExitCode::Data);
    } catch (const std::exception &error) {
        err << "hrt-tool: " << error.what() << "\n";
        return static_cast<int>(ExitCode::Internal);
    }
}
