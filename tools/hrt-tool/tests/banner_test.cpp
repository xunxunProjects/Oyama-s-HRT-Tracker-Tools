#include "banner.h"
#include "term.h"
#include "test_framework.h"

#include <cstddef>
#include <string>
#include <vector>

static TermCaps plain(int columns) {
    TermCaps caps;
    caps.tty = true;
    caps.columns = columns;
    return caps;
}

static TermCaps colored(int columns, bool faint) {
    TermCaps caps = plain(columns);
    caps.ansi = true;
    caps.color = true;
    caps.faint = faint;
    return caps;
}

static std::string strip_ansi(const std::string &text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
            while (i < text.size() && text[i] != 'm') {
                ++i;
            }
            continue;
        }
        out += text[i];
    }
    return out;
}

static std::vector<std::string> split_lines(const std::string &text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

static bool contains(const std::string &text, const std::string &needle) {
    return text.find(needle) != std::string::npos;
}

static void test_wide_plain() {
    const std::string banner = render_banner(plain(80), "0.1.1", "HINT");
    CHECK_EQ(banner.back(), '\n');
    CHECK_EQ(contains(banner, "\x1b"), false);
    const std::vector<std::string> lines = split_lines(banner);
    CHECK_EQ(lines.size(), std::size_t{6});
    if (lines.size() != 6) {
        return;
    }
    CHECK_EQ(lines[0], " _  _ ___ _____");
    CHECK_EQ(lines[1].substr(0, 16), "| || | _ \\_   _|");
    CHECK_EQ(lines[1].find("hrt-tool 0.1.1"), std::size_t{19});
    CHECK_EQ(lines[2].find("Oyama HRT Tracker terminal client"), std::size_t{19});
    CHECK_EQ(lines[3], "|_||_|_|_\\ |_|");
    CHECK_EQ(lines[4], "");
    CHECK_EQ(lines[5], "HINT");
    for (const std::string &line : lines) {
        CHECK(line.empty() || line.back() != ' ');
    }
}

static void test_wide_color() {
    const std::string plain_text = render_banner(plain(80), "0.1.1", "HINT");
    const std::string styled = render_banner(colored(80, true), "0.1.1", "HINT");
    CHECK_EQ(strip_ansi(styled), plain_text);
    CHECK(contains(styled, "\x1b[36m"));
    CHECK(contains(styled, "\x1b[1m"));
    CHECK(contains(styled, "\x1b[2m"));

    const std::string no_faint = render_banner(colored(80, false), "0.1.1", "HINT");
    CHECK_EQ(contains(no_faint, "\x1b[2m"), false);
    CHECK_EQ(strip_ansi(no_faint), plain_text);
    for (const std::string &line : split_lines(strip_ansi(styled))) {
        CHECK(line.empty() || line.back() != ' ');
    }
}

static void test_layout_boundary() {
    const std::vector<std::string> narrow = split_lines(render_banner(plain(51), "0.1.1", "HINT"));
    CHECK_EQ(narrow.size(), std::size_t{2});
    if (narrow.size() == 2) {
        CHECK_EQ(narrow[0], "hrt-tool 0.1.1");
        CHECK_EQ(narrow[1], "HINT");
    }
    CHECK_EQ(split_lines(render_banner(plain(52), "0.1.1", "HINT")).size(), std::size_t{6});

    const std::string long_version(100, '9');
    const std::vector<std::string> long_lines =
        split_lines(render_banner(plain(80), long_version, "HINT"));
    CHECK_EQ(long_lines.size(), std::size_t{2});
    if (long_lines.size() == 2) {
        CHECK_EQ(long_lines[0], "hrt-tool " + long_version);
        CHECK_EQ(long_lines[1], "HINT");
    }

    for (const int columns : {1, 0, -3}) {
        const std::string banner = render_banner(plain(columns), "0.1.1", "HINT");
        CHECK_EQ(split_lines(banner).size(), std::size_t{2});
        CHECK_EQ(banner.back(), '\n');
    }
}

static void test_hint_width() {
    const std::string russian = "Введите 'help' для списка команд, 'exit' для выхода.";
    const std::vector<std::string> narrow_hint = split_lines(render_banner(plain(60), "0.1.1", russian));
    CHECK_EQ(narrow_hint.size(), std::size_t{6});
    if (narrow_hint.size() == 6) {
        CHECK_EQ(narrow_hint[5], russian);
    }

    const std::string long_hint(200, 'h');
    const std::vector<std::string> long_lines = split_lines(render_banner(plain(80), "0.1.1", long_hint));
    CHECK_EQ(long_lines.size(), std::size_t{6});
    if (long_lines.size() == 6) {
        CHECK_EQ(long_lines[5], long_hint);
    }
}

static void test_console_welcome() {
    const TermCaps tty = plain(80);
    CHECK_EQ(console_welcome(tty, false, "custom", "HINT", "0.1.1"), std::string("custom\n"));
    CHECK_EQ(console_welcome(colored(80, true), false, "custom", "HINT", "0.1.1"),
             std::string("custom\n"));
    CHECK_EQ(console_welcome(tty, true, "custom", "HINT", "0.1.1"), std::string("custom\n"));

    CHECK_EQ(console_welcome(tty, true, "", "HINT", "0.1.1"), std::string("HINT\n"));

    TermCaps pipe;
    pipe.columns = 80;
    const std::string piped = console_welcome(pipe, false, "", "HINT", "0.1.1");
    CHECK_EQ(piped, std::string("HINT\n"));
    CHECK_EQ(contains(piped, "\x1b"), false);

    const std::string banner = console_welcome(tty, false, "", "HINT", "0.1.1");
    CHECK_EQ(banner, render_banner(tty, "0.1.1", "HINT"));
    CHECK_EQ(split_lines(banner).size(), std::size_t{6});
}

int main() {
    test_wide_plain();
    test_wide_color();
    test_layout_boundary();
    test_hint_width();
    test_console_welcome();
    return test_result();
}
