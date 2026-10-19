#include "prompt.h"

#include "error.h"

#include <iostream>

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#endif

std::string read_password(std::istream &in, std::ostream &out, const std::string &prompt,
                          bool confirm, bool no_echo) {
    const auto read_one = [&](const std::string &label) -> std::string {
        out << label;
        out.flush();

        std::string value;
        bool tty = no_echo && &in == &std::cin;
#ifndef _WIN32
        tty = tty && ::isatty(STDIN_FILENO) != 0;
        termios previous{};
        bool restored = false;
        if (tty && ::tcgetattr(STDIN_FILENO, &previous) == 0) {
            termios hidden = previous;
            hidden.c_lflag &= ~static_cast<tcflag_t>(ECHO);
            if (::tcsetattr(STDIN_FILENO, TCSANOW, &hidden) == 0) {
                restored = true;
            }
        }
        if (!std::getline(in, value)) {
            if (restored) {
                ::tcsetattr(STDIN_FILENO, TCSANOW, &previous);
            }
            throw ToolError(ExitCode::Usage, "PASSWORD_EMPTY", "no password was provided");
        }
        if (restored) {
            ::tcsetattr(STDIN_FILENO, TCSANOW, &previous);
            out << "\n";
        }
#else
        (void)tty;
        if (!std::getline(in, value)) {
            throw ToolError(ExitCode::Usage, "PASSWORD_EMPTY", "no password was provided");
        }
#endif
        if (!value.empty() && value.back() == '\r') {
            value.pop_back();
        }
        if (value.empty()) {
            throw ToolError(ExitCode::Usage, "PASSWORD_EMPTY", "password must not be empty");
        }
        return value;
    };

    const std::string first = read_one(prompt);
    if (confirm) {
        const std::string second = read_one("Confirm password: ");
        if (first != second) {
            throw ToolError(ExitCode::Usage, "PASSWORD_MISMATCH", "passwords do not match");
        }
    }
    return first;
}
