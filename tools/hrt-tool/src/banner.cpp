#include "banner.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace {

const std::array<std::string, 4> kWordmark = {
    " _  _ ___ _____",
    "| || | _ \\_   _|",
    "| __ |   / | |",
    "|_||_|_|_\\ |_|",
};
constexpr std::size_t kWordmarkWidth = 16;
constexpr std::size_t kGap = 3;
const char *const kProduct = "Oyama HRT Tracker terminal client";

}  // namespace

std::string render_banner(const TermCaps &caps, const std::string &version,
                          const std::string &hint) {
    const std::string title = "hrt-tool " + version;
    const std::string product = kProduct;
    const std::size_t needed =
        kWordmarkWidth + kGap + std::max(utf8_width(title), utf8_width(product));
    const bool wide = caps.columns > 0 && static_cast<std::size_t>(caps.columns) >= needed;

    std::string out;
    if (!wide) {
        out += term_style(caps, Role::Title, title) + "\n";
        out += hint + "\n";
        return out;
    }

    const std::array<std::string, 4> texts = {"", title, product, ""};
    const std::array<Role, 4> roles = {Role::Plain, Role::Title, Role::Muted, Role::Plain};
    for (std::size_t row = 0; row < kWordmark.size(); ++row) {
        out += term_style(caps, Role::Accent, kWordmark[row]);
        if (!texts[row].empty()) {
            const std::size_t art_width = utf8_width(kWordmark[row]);
            const std::size_t fill = art_width < kWordmarkWidth ? kWordmarkWidth - art_width : 0;
            out += std::string(fill + kGap, ' ');
            out += term_style(caps, roles[row], texts[row]);
        }
        out += "\n";
    }
    out += "\n";
    out += hint + "\n";
    return out;
}

std::string console_welcome(const TermCaps &caps, bool json, const std::string &custom,
                            const std::string &hint, const std::string &version) {
    if (!custom.empty()) {
        return custom + "\n";
    }
    if (json || !caps.tty) {
        return hint + "\n";
    }
    return render_banner(caps, version, hint);
}
