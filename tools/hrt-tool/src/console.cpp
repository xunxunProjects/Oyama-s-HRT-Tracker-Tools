#include "console.h"

#include "cli.h"
#include "i18n.h"
#include "link.h"
#include "net_error.h"
#include "paths.h"
#include "session.h"
#include "vault.h"

#include <cctype>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

bool stream_is_tty(std::ostream &out) {
    if (&out != &std::cout) {
        return false;
    }
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return ::isatty(STDOUT_FILENO) != 0;
#endif
}

void save_context(const GlobalOptions &options, SessionContext &context) {
    const Paths paths = resolve_paths(options.config_dir);
    Vault::save(paths.vault_file, context.session.data, context.master);
}

void report_error(const ToolError &error, std::ostream &err) {
    err << "hrt-tool: " << error.what() << " [" << error.code() << "]\n";
}

void report_error(const std::exception &error, std::ostream &err) {
    err << "hrt-tool: " << error.what() << "\n";
}

void report_error(const NetworkFailure &error, Language language,
                  const VaultData &data, bool debug, bool trace, bool no_humor,
                  std::ostream &err) {
    if (should_print_human_line(debug, trace, no_humor, data.humor)) {
        const std::string human = failure_human_line(language, error.kind());
        if (!human.empty()) {
            err << human << "\n";
        }
    }
    err << "hrt-tool: " << error.what() << " [" << error.code() << "]\n";
}

} // namespace

std::vector<std::string> tokenize_console_line(const std::string &line,
                                               std::string *error) {
    std::vector<std::string> tokens;
    if (error != nullptr) {
        error->clear();
    }
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }
        if (i >= line.size() || line[i] == '#') {
            break;
        }

        std::string token;
        bool quoted = false;
        char quote = 0;
        while (i < line.size()) {
            const char c = line[i];
            if (quoted) {
                if (c == '\\' && i + 1 < line.size()) {
                    token.push_back(line[++i]);
                    ++i;
                    continue;
                }
                if (c == quote) {
                    quoted = false;
                    ++i;
                    continue;
                }
                token.push_back(c);
                ++i;
                continue;
            }
            if (c == '\'' || c == '"') {
                quoted = true;
                quote = c;
                ++i;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(c))) {
                break;
            }
            if (c == '\\' && i + 1 < line.size()) {
                token.push_back(line[++i]);
                ++i;
                continue;
            }
            token.push_back(c);
            ++i;
        }
        if (quoted) {
            if (error != nullptr) {
                *error = "unterminated quote";
            }
            return {};
        }
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

int run_console(const GlobalOptions &options, std::istream &in, std::ostream &out,
                std::ostream &err) {
    GlobalOptions session_options = options;
    SessionContext context;
    UnlockedVault console_lock;
    try {
        /* Keep the vault lock for the console's lifetime so a concurrent
           one-shot command cannot update the vault behind our in-memory
           session. Commands that reuse the loaded context skip re-locking. */
        console_lock = unlock_full(session_options, in, out, &context);
    } catch (const ToolError &error) {
        report_error(error, err);
        return static_cast<int>(error.exit_code());
    }

    Language language = Language::En;
    try {
        language = parse_language(context.session.data.language);
    } catch (const ToolError &) {
        language = Language::En;
    }
    const std::string welcome = context.session.data.welcome.empty()
                                    ? tr(language, I18nKey::Welcome)
                                    : context.session.data.welcome;
    out << welcome << "\n";
    out.flush();

    std::string current = context.session.data.active_link;
    while (true) {
        out << (current.empty() ? "hrt> " : "hrt$" + current + "> ");
        out.flush();

        std::string line;
        if (!std::getline(in, line)) {
            break;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::string token_error;
        const std::vector<std::string> tokens =
            tokenize_console_line(line, &token_error);
        if (!token_error.empty()) {
            err << "hrt-tool: " << token_error << "\n";
            continue;
        }
        if (tokens.empty()) {
            continue;
        }
        const std::string &command = tokens[0];

        if (command == "exit" || command == "quit") {
            if (!current.empty()) {
                try {
                    select_link(context.session.data, "");
                    save_context(session_options, context);
                    current.clear();
                } catch (const ToolError &error) {
                    report_error(error, err);
                }
                continue;
            }
            return 0;
        }
        if (command == "help") {
            print_help(out);
            continue;
        }
        if (command == "clear") {
            if (stream_is_tty(out) && !session_options.no_color) {
                out << "\x1b[2J\x1b[H";
            }
            continue;
        }
        if (command == "debug") {
            if (tokens.size() != 2 ||
                (tokens[1] != "on" && tokens[1] != "off")) {
                err << "hrt-tool: debug needs on or off [USAGE]\n";
                continue;
            }
            session_options.debug = tokens[1] == "on";
            out << "debug: " << tokens[1] << "\n";
            continue;
        }
        if (command == "init") {
            err << tr(language, I18nKey::ConsoleInitRefused) << "\n";
            continue;
        }
        if (command == "console") {
            err << "hrt-tool: already in the console [USAGE]\n";
            continue;
        }
        if (command == "commit-server") {
            if (tokens.size() != 2) {
                err << "hrt-tool: commit-server needs a link name [USAGE]\n";
                continue;
            }
            const std::string name =
                tokens[1] == "-" ? std::string{} : tokens[1];
            try {
                select_link(context.session.data, name);
                save_context(session_options, context);
                current = context.session.data.active_link;
            } catch (const ToolError &error) {
                report_error(error, err);
            }
            continue;
        }

        std::vector<std::string> argv;
        argv.push_back("hrt-tool");
        for (const std::string &token : tokens) {
            argv.push_back(token);
        }
        std::vector<const char *> raw;
        raw.reserve(argv.size());
        for (const std::string &arg : argv) {
            raw.push_back(arg.c_str());
        }

        try {
            ParsedArgs parsed =
                parse_cli_args(static_cast<int>(raw.size()), raw.data());
            if (command != "f2f") {
                if (!parsed.options.server_explicit) {
                    parsed.options.server = context.session.data.server;
                }
                if (parsed.options.mode.empty()) {
                    parsed.options.mode = context.session.data.mode;
                }
                parsed.options.config_dir = session_options.config_dir;
                parsed.options.debug =
                    parsed.options.debug || session_options.debug;
                parsed.options.trace =
                    parsed.options.trace || session_options.trace;
                parsed.options.no_humor =
                    parsed.options.no_humor || session_options.no_humor;
            }
            dispatch(parsed, in, out, err, &context);
        } catch (const NetworkFailure &error) {
            report_error(error, language, context.session.data,
                         session_options.debug, session_options.trace,
                         session_options.no_humor, err);
        } catch (const ToolError &error) {
            report_error(error, err);
        } catch (const std::exception &error) {
            report_error(error, err);
        }

        try {
            language = parse_language(context.session.data.language);
        } catch (const ToolError &) {
            language = Language::En;
        }
        current = context.session.data.active_link;
    }
    return 0;
}
