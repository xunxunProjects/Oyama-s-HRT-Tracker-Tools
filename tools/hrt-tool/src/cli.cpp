#include "cli.h"

#include "commands.h"
#include "error.h"

#include <nlohmann/json.hpp>
#include <string>

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
            } else if (name == "--proxy") {
                parsed.options.proxy = take_value();
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
            } else if (name == "--trust-server") {
                boolean_flag();
                parsed.options.trust_server = true;
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

void print_help(std::ostream &out) {
    out << "hrt-tool — Oyama HRT Tracker terminal client (M1 skeleton)\n\n"
        << "Usage: hrt-tool [global flags] <command> [command flags]\n\n"
        << "Commands:\n"
        << "  init                 create the encrypted local vault\n"
        << "  login                log in (not implemented in M1)\n"
        << "  logout               revoke the session (not implemented in M1)\n"
        << "  whoami               show the current account (not implemented in M1)\n"
        << "  look                 show concentrations (not implemented in M1)\n"
        << "  add                  add a dose (not implemented in M1)\n"
        << "  dose ...             manage doses (not implemented in M1)\n"
        << "  lab ...              manage labs (not implemented in M1)\n"
        << "  weight ...           manage weight (not implemented in M1)\n"
        << "  export               export data (not implemented in M1)\n"
        << "  import               import data (not implemented in M1)\n"
        << "  snapshot ...         encrypted snapshot (not implemented in M1)\n"
        << "  config path          print config paths\n"
        << "  logs show            decrypt an encrypted log file\n"
        << "  version              print versions\n\n"
        << "Global flags:\n"
        << "  --server URL  --proxy URL  --config-dir DIR  --debug  --trace\n"
        << "  --json  --no-color  --yes  --timeout SECONDS  --trust-server\n"
        << "  --insecure-tls  --transport auto|tls|openssl-v1\n"
        << "  --no-cache-cloud-key  --compute local|server\n"
        << "  --vault-password-file FILE  --vault-password-stdin  --log-file FILE\n"
        << "  --snapshot FILE  --mode transfem|transmasc  --utc\n"
        << "  --user-id ID  --account-password-file FILE  --account-password-stdin\n\n"
        << "Plaintext is written only to stdout. All local state is encrypted at rest.\n";
}

int run_cli(int argc, const char *const *argv, std::istream &in, std::ostream &out,
            std::ostream &err) {
    try {
        const ParsedArgs parsed = parse_args(argc, argv);
        if (parsed.help || parsed.command.empty()) {
            print_help(out);
            return 0;
        }
        return dispatch(parsed, in, out, err);
    } catch (const ToolError &error) {
        err << "hrt-tool: " << error.what() << " [" << error.code() << "]\n";
        return static_cast<int>(error.exit_code());
    } catch (const nlohmann::json::exception &error) {
        err << "hrt-tool: malformed data: " << error.what() << " [DATA]\n";
        return static_cast<int>(ExitCode::Data);
    } catch (const std::exception &error) {
        err << "hrt-tool: " << error.what() << "\n";
        return 1;
    }
}
