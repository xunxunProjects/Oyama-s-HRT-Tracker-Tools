#include "term.h"
#include "test_framework.h"

#include <cstddef>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <utility>

static TermEnv make_env(bool tty, std::map<std::string, std::string> vars) {
    TermEnv env;
    env.is_tty = tty;
    env.vars = std::move(vars);
    return env;
}

static int columns_for(int hint, const std::map<std::string, std::string> &vars) {
    TermEnv env = make_env(true, vars);
    env.columns_hint = hint;
    return term_caps(env).columns;
}

static bool unicode_for(const std::map<std::string, std::string> &vars) {
    return term_caps(make_env(true, vars)).unicode;
}

static void test_ansi_color() {
    {
        const TermCaps caps = term_caps(make_env(false, {{"TERM", "xterm"}}));
        CHECK_EQ(caps.tty, false);
        CHECK_EQ(caps.ansi, false);
        CHECK_EQ(caps.color, false);
        CHECK_EQ(caps.faint, false);
    }
    {
        const TermCaps caps = term_caps(make_env(true, {{"TERM", "xterm-256color"}}));
        CHECK_EQ(caps.tty, true);
        CHECK_EQ(caps.ansi, true);
        CHECK_EQ(caps.color, true);
        CHECK_EQ(caps.faint, true);
    }
    for (const char *value : {"1", "0"}) {
        const TermCaps caps =
            term_caps(make_env(true, {{"TERM", "xterm"}, {"NO_COLOR", value}}));
        CHECK_EQ(caps.ansi, true);
        CHECK_EQ(caps.color, false);
        CHECK_EQ(caps.faint, false);
    }
    {
        const TermCaps caps =
            term_caps(make_env(true, {{"TERM", "xterm"}, {"NO_COLOR", ""}}));
        CHECK_EQ(caps.ansi, true);
        CHECK_EQ(caps.color, true);
    }
    {
        TermEnv env = make_env(true, {{"TERM", "xterm"}});
        env.no_color_flag = true;
        const TermCaps caps = term_caps(env);
        CHECK_EQ(caps.tty, true);
        CHECK_EQ(caps.ansi, false);
        CHECK_EQ(caps.color, false);
    }
    for (const char *term : {"dumb", ""}) {
        const TermCaps caps = term_caps(make_env(true, {{"TERM", term}}));
        CHECK_EQ(caps.ansi, false);
        CHECK_EQ(caps.color, false);
    }
    {
        const TermCaps caps = term_caps(make_env(true, {}));
        CHECK_EQ(caps.ansi, false);
        CHECK_EQ(caps.color, false);
    }
    {
        const TermCaps caps = term_caps(make_env(true, {{"TERM", "linux"}}));
        CHECK_EQ(caps.ansi, true);
        CHECK_EQ(caps.color, true);
        CHECK_EQ(caps.faint, false);
    }
    {
        TermEnv env = make_env(true, {{"TERM", "xterm"}});
        env.vt_ready = false;
        const TermCaps caps = term_caps(env);
        CHECK_EQ(caps.ansi, false);
        CHECK_EQ(caps.color, false);
    }
}

static void test_columns() {
    CHECK_EQ(columns_for(132, {{"COLUMNS", "100"}}), 132);
    CHECK_EQ(columns_for(0, {{"COLUMNS", "100"}}), 100);
    CHECK_EQ(columns_for(0, {{"COLUMNS", "1000"}}), 1000);
    CHECK_EQ(columns_for(0, {{"COLUMNS", "1"}}), 1);
    for (const char *junk : {"abc", "0", "-5", "99999", "12x", " 100", ""}) {
        CHECK_EQ(columns_for(0, {{"COLUMNS", junk}}), 80);
    }
    CHECK_EQ(columns_for(0, {}), 80);
}

static void test_unicode() {
    CHECK_EQ(unicode_for({{"LC_ALL", "C.UTF-8"}}), true);
    CHECK_EQ(unicode_for({{"LC_ALL", ""}, {"LANG", "en_US.utf8"}}), true);
    CHECK_EQ(unicode_for({{"LC_ALL", "C"}, {"LANG", "en_US.UTF-8"}}), false);
    CHECK_EQ(unicode_for({{"LC_CTYPE", "de_DE.UTF-8"}}), true);
    CHECK_EQ(unicode_for({{"LC_CTYPE", "C"}, {"LANG", "en_US.UTF-8"}}), false);
    CHECK_EQ(unicode_for({{"LANG", "en_US.UTF8"}}), true);
    CHECK_EQ(unicode_for({}), false);
    CHECK_EQ(unicode_for({{"TERM", "linux"}, {"LANG", "C.UTF-8"}}), false);
    CHECK_EQ(unicode_for({{"TERM", "xterm"}, {"LANG", "C.UTF-8"}}), true);
}

static void test_style() {
    TermCaps caps;
    caps.tty = true;
    caps.ansi = true;
    caps.color = true;
    caps.faint = true;
    CHECK_EQ(term_style(caps, Role::Plain, "x"), std::string("x"));
    CHECK_EQ(term_style(caps, Role::Title, "x"), std::string("\x1b[1mx\x1b[0m"));
    CHECK_EQ(term_style(caps, Role::Accent, "x"), std::string("\x1b[36mx\x1b[0m"));
    CHECK_EQ(term_style(caps, Role::Muted, "x"), std::string("\x1b[2mx\x1b[0m"));
    CHECK_EQ(term_style(caps, Role::Ok, "x"), std::string("\x1b[32mx\x1b[0m"));
    CHECK_EQ(term_style(caps, Role::Warn, "x"), std::string("\x1b[33mx\x1b[0m"));
    CHECK_EQ(term_style(caps, Role::Error, "x"), std::string("\x1b[31mx\x1b[0m"));

    for (Role role : {Role::Plain, Role::Title, Role::Accent, Role::Muted, Role::Ok,
                      Role::Warn, Role::Error}) {
        CHECK_EQ(term_style(caps, role, ""), std::string());
    }

    TermCaps no_faint = caps;
    no_faint.faint = false;
    CHECK_EQ(term_style(no_faint, Role::Muted, "x"), std::string("x"));
    CHECK_EQ(term_style(no_faint, Role::Title, "x"), std::string("\x1b[1mx\x1b[0m"));

    TermCaps no_color = caps;
    no_color.color = false;
    for (Role role : {Role::Plain, Role::Title, Role::Accent, Role::Muted, Role::Ok,
                      Role::Warn, Role::Error}) {
        CHECK_EQ(term_style(no_color, role, "x"), std::string("x"));
    }
}

static void test_width() {
    CHECK_EQ(utf8_width(""), std::size_t{0});
    CHECK_EQ(utf8_width("abc"), std::size_t{3});
    CHECK_EQ(utf8_width("Привет"), std::size_t{6});
    CHECK_EQ(utf8_width("中文"), std::size_t{4});
    CHECK_EQ(utf8_width("ひら"), std::size_t{4});
    CHECK_EQ(utf8_width("Ａ"), std::size_t{2});
    CHECK_EQ(utf8_width(std::string("e\xcc\x81")), std::size_t{1});     // e + U+0301
    CHECK_EQ(utf8_width(std::string("\xe2\x80\x8b")), std::size_t{0});  // U+200B
    CHECK_EQ(utf8_width(std::string("\xf0\x9f\x98\x80")), std::size_t{2}); // U+1F600
    CHECK_EQ(utf8_width(std::string("\xff")), std::size_t{1});
    CHECK_EQ(utf8_width(std::string("a\xe4")), std::size_t{2});     // truncated sequence
    CHECK_EQ(utf8_width(std::string("\xe4" "a")), std::size_t{2});  // lead byte, no continuation
    CHECK_EQ(utf8_width(std::string("\xc0\x80")), std::size_t{2});  // overlong NUL
    CHECK_EQ(utf8_width(std::string("\xed\xa0\x80")), std::size_t{3});     // surrogate U+D800
    CHECK_EQ(utf8_width(std::string("\xf4\x90\x80\x80")), std::size_t{4}); // above U+10FFFF
    CHECK_EQ(utf8_width(std::string("\x07")), std::size_t{0});
    CHECK_EQ(utf8_width(std::string("\x7f")), std::size_t{0});
}

#ifndef _WIN32
// std::cout is deliberately not asserted on: it is a TTY when the test is run by hand.
static void test_process_adapter() {
    setenv("COLUMNS", "100", 1);
    setenv("TERM", "xterm", 1);
    unsetenv("NO_COLOR");
    unsetenv("LC_ALL");
    {
        std::ostringstream sink;
        CHECK_EQ(term_env_for(sink, false).is_tty, false);
        const TermCaps caps = term_caps_for(sink, false);
        CHECK_EQ(caps.tty, false);
        CHECK_EQ(caps.ansi, false);
        CHECK_EQ(caps.color, false);
        CHECK_EQ(caps.columns, 100);
    }
    {
        std::ostringstream sink;
        CHECK_EQ(term_env_for(sink, true).no_color_flag, true);
        CHECK_EQ(term_env_for(sink, false).no_color_flag, false);
    }
    {
        std::ostringstream sink;
        const TermEnv env = term_env_for(sink, false);
        CHECK_EQ(env.vars.count("NO_COLOR"), std::size_t{0});
        CHECK_EQ(env.vars.count("LC_ALL"), std::size_t{0});
        CHECK_EQ(env.vars.at("TERM"), std::string("xterm"));
        CHECK_EQ(env.vars.at("COLUMNS"), std::string("100"));
    }
    setenv("NO_COLOR", "1", 1);
    {
        std::ostringstream sink;
        const TermEnv env = term_env_for(sink, false);
        CHECK_EQ(env.vars.count("NO_COLOR"), std::size_t{1});
        CHECK_EQ(env.vars.at("NO_COLOR"), std::string("1"));
    }
    unsetenv("NO_COLOR");
}
#endif

int main() {
    test_ansi_color();
    test_columns();
    test_unicode();
    test_style();
    test_width();
#ifndef _WIN32
    test_process_adapter();
#endif
    return test_result();
}
