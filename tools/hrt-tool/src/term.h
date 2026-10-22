#pragma once

#include <cstddef>
#include <iosfwd>
#include <map>
#include <string>

struct TermCaps {
    bool tty = false;     // stream is an interactive terminal
    bool ansi = false;    // escape sequences are safe (clear, cursor control)
    bool color = false;   // ansi and NO_COLOR is unset or empty
    bool faint = false;   // color and the terminal renders SGR 2 usefully
    bool unicode = false; // UTF-8 locale on a terminal that can draw it
    int columns = 80;
};

struct TermEnv {
    bool is_tty = false;
    int columns_hint = 0; // width reported by the terminal, 0 when unknown
    bool vt_ready = true; // false on a Windows console without VT processing
    bool no_color_flag = false;
    std::map<std::string, std::string> vars; // NO_COLOR TERM COLUMNS LC_ALL LC_CTYPE LANG
};

TermCaps term_caps(const TermEnv &env);

// Only std::cout (fd 1) and std::cerr (fd 2) can be terminals; any other stream is not.
TermEnv term_env_for(const std::ostream &out, bool no_color_flag);
TermCaps term_caps_for(const std::ostream &out, bool no_color_flag);

// Windows: enable VT processing and UTF-8 output on real consoles. Elsewhere: no-op.
void term_platform_init();

enum class Role { Plain, Title, Accent, Muted, Ok, Warn, Error };

std::string term_style(const TermCaps &caps, Role role, const std::string &text);

std::size_t utf8_width(const std::string &text); // display columns, not bytes
