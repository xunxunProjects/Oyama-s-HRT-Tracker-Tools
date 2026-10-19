#include "prompt.h"

#include "entropy.h"
#include "error.h"

#include <iostream>

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#endif

namespace {

#ifndef _WIN32
class EchoGuard {
public:
    explicit EchoGuard(bool enabled) {
        if (!enabled) {
            return;
        }
        if (::tcgetattr(STDIN_FILENO, &previous_) != 0) {
            return;
        }
        termios hidden = previous_;
        hidden.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        if (::tcsetattr(STDIN_FILENO, TCSANOW, &hidden) == 0) {
            active_ = true;
        }
    }

    ~EchoGuard() {
        if (active_) {
            ::tcsetattr(STDIN_FILENO, TCSANOW, &previous_);
        }
    }

    EchoGuard(const EchoGuard &) = delete;
    EchoGuard &operator=(const EchoGuard &) = delete;

private:
    termios previous_{};
    bool active_ = false;
};
#endif

} // namespace

std::string read_password_line(std::istream &in, size_t max_bytes) {
    std::string value;
    char c = '\0';
    while (in.get(c)) {
        if (c == '\n') {
            break;
        }
        value.push_back(c);
        if (value.size() > max_bytes) {
            throw ToolLimitError("password exceeds the maximum length");
        }
    }
    if (!value.empty() && value.back() == '\r') {
        value.pop_back();
    }
    return value;
}

std::string read_password(std::istream &in, std::ostream &out, const std::string &prompt,
                          bool confirm, bool no_echo) {
    const auto read_one = [&](const std::string &label) -> std::string {
        out << label;
        out.flush();

        std::string value;
        bool tty = no_echo && &in == &std::cin;
#ifndef _WIN32
        tty = tty && ::isatty(STDIN_FILENO) != 0;
        EchoGuard guard(tty);
        value = read_password_line(in, kMaxMasterPasswordBytes);
        if (tty) {
            out << "\n";
        }
#else
        (void)tty;
        value = read_password_line(in, kMaxMasterPasswordBytes);
#endif
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
