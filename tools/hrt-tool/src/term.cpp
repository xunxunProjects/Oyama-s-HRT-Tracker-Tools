#include "term.h"

#include <cstdint>
#include <initializer_list>

namespace {

struct Range {
    std::uint32_t first;
    std::uint32_t last;
};

const Range kZeroWidth[] = {
    {0x0300, 0x036F}, {0x200B, 0x200F}, {0x20D0, 0x20FF},
    {0x3099, 0x309A}, {0xFE00, 0xFE0F}, {0xFEFF, 0xFEFF},
};

const Range kWide[] = {
    {0x1100, 0x115F},   {0x2E80, 0x303E},   {0x3041, 0x33FF},   {0x3400, 0x4DBF},
    {0x4E00, 0x9FFF},   {0xA000, 0xA4CF},   {0xAC00, 0xD7A3},   {0xF900, 0xFAFF},
    {0xFE30, 0xFE6F},   {0xFF00, 0xFF60},   {0xFFE0, 0xFFE6},   {0x1F300, 0x1F64F},
    {0x1F680, 0x1F6FF}, {0x1F900, 0x1F9FF}, {0x20000, 0x3FFFD},
};

template <std::size_t N>
bool in_ranges(const Range (&ranges)[N], std::uint32_t cp) {
    for (const Range &range : ranges) {
        if (cp >= range.first && cp <= range.last) {
            return true;
        }
    }
    return false;
}

std::size_t code_point_width(std::uint32_t cp) {
    if (cp < 0x20 || cp == 0x7F || in_ranges(kZeroWidth, cp)) {
        return 0;
    }
    return in_ranges(kWide, cp) ? 2 : 1;
}

std::string var_or_empty(const TermEnv &env, const char *name) {
    const auto it = env.vars.find(name);
    return it == env.vars.end() ? std::string() : it->second;
}

int parse_columns(const std::string &text) {
    if (text.empty() || text.size() > 4) {
        return 0;
    }
    int value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            return 0;
        }
        value = value * 10 + (c - '0');
    }
    return value >= 1 && value <= 1000 ? value : 0;
}

std::string ascii_lower(std::string text) {
    for (char &c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

bool locale_is_utf8(const TermEnv &env) {
    for (const char *name : {"LC_ALL", "LC_CTYPE", "LANG"}) {
        const std::string value = ascii_lower(var_or_empty(env, name));
        if (value.empty()) {
            continue;
        }
        return value.find("utf-8") != std::string::npos ||
               value.find("utf8") != std::string::npos;
    }
    return false;
}

} // namespace

TermCaps term_caps(const TermEnv &env) {
    TermCaps caps;
    caps.tty = env.is_tty;

    const std::string term = var_or_empty(env, "TERM");
    caps.ansi = env.is_tty && env.vt_ready && !env.no_color_flag && !term.empty() &&
                term != "dumb";
    caps.color = caps.ansi && var_or_empty(env, "NO_COLOR").empty();

    const bool kernel_console = term == "linux";
    caps.faint = caps.color && !kernel_console;
    caps.unicode = locale_is_utf8(env) && !kernel_console;

    const int parsed = parse_columns(var_or_empty(env, "COLUMNS"));
    caps.columns = env.columns_hint > 0 ? env.columns_hint : (parsed > 0 ? parsed : 80);
    return caps;
}

std::string term_style(const TermCaps &caps, Role role, const std::string &text) {
    if (text.empty() || !caps.color) {
        return text;
    }
    const char *code = nullptr;
    switch (role) {
    case Role::Plain:
        break;
    case Role::Title:
        code = "1";
        break;
    case Role::Accent:
        code = "36";
        break;
    case Role::Muted:
        code = caps.faint ? "2" : nullptr;
        break;
    case Role::Ok:
        code = "32";
        break;
    case Role::Warn:
        code = "33";
        break;
    case Role::Error:
        code = "31";
        break;
    }
    if (code == nullptr) {
        return text;
    }
    return std::string("\x1b[") + code + "m" + text + "\x1b[0m";
}

std::size_t utf8_width(const std::string &text) {
    std::size_t width = 0;
    std::size_t i = 0;
    const std::size_t size = text.size();
    while (i < size) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = 0;
        std::size_t len = 0;
        if (lead < 0x80) {
            cp = lead;
            len = 1;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            cp = lead & 0x1Fu;
            len = 2;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            cp = lead & 0x0Fu;
            len = 3;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            cp = lead & 0x07u;
            len = 4;
        }

        bool valid = len != 0 && i + len <= size;
        for (std::size_t k = 1; valid && k < len; ++k) {
            const unsigned char next = static_cast<unsigned char>(text[i + k]);
            valid = (next & 0xC0) == 0x80;
            cp = (cp << 6) | (next & 0x3Fu);
        }
        if (valid && len == 3) {
            valid = cp >= 0x800 && !(cp >= 0xD800 && cp <= 0xDFFF);
        } else if (valid && len == 4) {
            valid = cp >= 0x10000 && cp <= 0x10FFFF;
        }

        if (!valid) {
            ++width;
            ++i;
            continue;
        }
        width += code_point_width(cp);
        i += len;
    }
    return width;
}
