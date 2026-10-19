#include "entropy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr double kMasterPasswordFloorBits = 40.0;

struct Charset {
    bool lower = false;
    bool upper = false;
    bool digits = false;
    bool symbols = false;
    bool non_ascii = false;
    bool only_spaces = false;
};

struct Pattern {
    size_t length;
    double weight;
};

const std::set<std::string> &common_passwords() {
    static const std::set<std::string> kCommon = {
        "password",    "passw0rd",   "p@ssw0rd",  "iloveyou",  "woaini",
        "woaiwojia",   "aini",       "qwerty",    "qwertyuiop", "asdfgh",
        "asdfghjkl",   "zxcvbnm",    "qazwsx",    "1qaz2wsx",  "zaq12wsx",
        "1q2w3e4r",    "q1w2e3r4",   "abc",       "abcd",      "abcdef",
        "abcdefg",     "abcdefgh",   "admin",     "administrator", "root",
        "letmein",     "welcome",    "monkey",    "dragon",    "master",
        "sunshine",    "princess",   "football",  "baseball",  "superman",
        "batman",      "shadow",     "michael",   "qq",        "wang",
        "zhang",       "li",         "liu",       "chen",      "hrt",
        "estradiol",   "estrogen",   "trans",     "hello",     "test",
        "guest",       "secret",     "changeme",  "mima",      "woshishui",
    };
    return kCommon;
}

const std::vector<std::string> &common_words() {
    static const std::vector<std::string> kWords = [] {
        std::vector<std::string> words;
        for (const std::string &word : common_passwords()) {
            if (word.size() >= 4) {
                words.push_back(word);
            }
        }
        std::sort(words.begin(), words.end(),
                  [](const std::string &a, const std::string &b) { return a.size() > b.size(); });
        return words;
    }();
    return kWords;
}

const std::vector<std::string> &keyboard_rows() {
    static const std::vector<std::string> kRows = {
        "qwertyuiop", "asdfghjkl", "zxcvbnm", "1234567890",
        "!@#$%^&*()", "abcdefghijklmnopqrstuvwxyz", "1qaz", "2wsx",
        "3edc",       "4rfv",      "5tgb",    "6yhn", "7ujm", "8ik,",
        "9ol.",       "0p;/",
    };
    return kRows;
}

char32_t leet(char32_t c) {
    switch (c) {
    case '0': return 'o';
    case '@': return 'a';
    case '4': return 'a';
    case '$': return 's';
    case '5': return 's';
    case '3': return 'e';
    case '!': return 'i';
    case '7': return 't';
    case '+': return 't';
    default: return c;
    }
}

std::vector<char32_t> decode_utf8(const std::string &text) {
    std::vector<char32_t> out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        char32_t cp = 0;
        size_t len = 0;
        if (c < 0x80) {
            cp = c;
            len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            len = 4;
        } else {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        if (i + len > text.size()) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        bool valid = true;
        for (size_t j = 1; j < len; ++j) {
            const unsigned char next = static_cast<unsigned char>(text[i + j]);
            if ((next & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (next & 0x3F);
        }
        if (!valid || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

char32_t ascii_lower_cp(char32_t c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 'a';
    }
    return c;
}

std::string ascii_lower(const std::string &text) {
    std::string out = text;
    for (char &c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

bool is_ascii_digit(char c) {
    return c >= '0' && c <= '9';
}

bool is_date_separator(char c) {
    return c == '-' || c == '.' || c == '/';
}

int two_digits(const std::string &text, size_t pos) {
    if (pos + 1 >= text.size() || !is_ascii_digit(text[pos]) || !is_ascii_digit(text[pos + 1])) {
        return -1;
    }
    return (text[pos] - '0') * 10 + (text[pos + 1] - '0');
}

int four_digit_year(const std::string &text, size_t pos) {
    if (pos + 3 >= text.size()) {
        return -1;
    }
    for (size_t i = 0; i < 4; ++i) {
        if (!is_ascii_digit(text[pos + i])) {
            return -1;
        }
    }
    return (text[pos] - '0') * 1000 + (text[pos + 1] - '0') * 100 +
           (text[pos + 2] - '0') * 10 + (text[pos + 3] - '0');
}

size_t date_length_at(const std::string &text, size_t start) {
    // Year-month-day.
    if (start + 4 <= text.size()) {
        const int year = four_digit_year(text, start);
        if (year >= 1900 && year <= 2099) {
            size_t pos = start + 4;
            if (pos < text.size() && is_date_separator(text[pos])) {
                ++pos;
            }
            const int month = two_digits(text, pos);
            if (month >= 1 && month <= 12) {
                pos += 2;
                if (pos < text.size() && is_date_separator(text[pos])) {
                    ++pos;
                }
                const int day = two_digits(text, pos);
                if (day >= 1 && day <= 31) {
                    return pos + 2 - start;
                }
            }
        }
    }

    // Day-month-year.
    if (start + 2 <= text.size()) {
        const int day = two_digits(text, start);
        if (day >= 1 && day <= 31) {
            size_t pos = start + 2;
            if (pos < text.size() && is_date_separator(text[pos])) {
                ++pos;
            }
            const int month = two_digits(text, pos);
            if (month >= 1 && month <= 12) {
                pos += 2;
                if (pos < text.size() && is_date_separator(text[pos])) {
                    ++pos;
                }
                const int year = four_digit_year(text, pos);
                if (year >= 1900 && year <= 2099) {
                    return pos + 4 - start;
                }
            }
        }
    }

    // Month-day-year.
    if (start + 2 <= text.size()) {
        const int month = two_digits(text, start);
        if (month >= 1 && month <= 12) {
            size_t pos = start + 2;
            if (pos < text.size() && is_date_separator(text[pos])) {
                ++pos;
            }
            const int day = two_digits(text, pos);
            if (day >= 1 && day <= 31) {
                pos += 2;
                if (pos < text.size() && is_date_separator(text[pos])) {
                    ++pos;
                }
                const int year = four_digit_year(text, pos);
                if (year >= 1900 && year <= 2099) {
                    return pos + 4 - start;
                }
            }
        }
    }

    return 0;
}

bool is_symbol_cp(char32_t cp) {
    return cp == 0x20 || (cp >= 0x21 && cp <= 0x2F) || (cp >= 0x3A && cp <= 0x40) ||
           (cp >= 0x5B && cp <= 0x60) || (cp >= 0x7B && cp <= 0x7E);
}

bool is_non_ascii_cp(char32_t cp) {
    return cp < 0x20 || cp > 0x7E;
}

Charset analyze_charset(const std::vector<char32_t> &chars) {
    Charset charset;
    bool non_space_symbol = false;
    for (char32_t cp : chars) {
        if (cp >= 'a' && cp <= 'z') {
            charset.lower = true;
        } else if (cp >= 'A' && cp <= 'Z') {
            charset.upper = true;
        } else if (cp >= '0' && cp <= '9') {
            charset.digits = true;
        } else if (is_symbol_cp(cp)) {
            charset.symbols = true;
            if (cp != 0x20) {
                non_space_symbol = true;
            }
        } else if (is_non_ascii_cp(cp)) {
            charset.non_ascii = true;
        }
    }
    charset.only_spaces = charset.symbols && !non_space_symbol;
    return charset;
}

int charset_size(const Charset &charset) {
    int size = 0;
    if (charset.lower) size += 26;
    if (charset.upper) size += 26;
    if (charset.digits) size += 10;
    if (charset.symbols) size += 33;
    if (charset.non_ascii) size += 100;
    return size;
}

int pool_size(const Charset &charset) {
    int size = charset_size(charset);
    if (charset.symbols) {
        size -= 33;
    }
    if (charset.only_spaces) {
        size += 1;
    } else if (charset.symbols) {
        size += 33;
    }
    return std::max(size, 10);
}

bool is_step(char32_t a, char32_t b) {
    const char32_t lower_a = ascii_lower_cp(a);
    const char32_t lower_b = ascii_lower_cp(b);
    if (lower_a > lower_b) {
        return is_step(lower_b, lower_a);
    }
    if (lower_b - lower_a == 1) {
        return true;
    }
    if (lower_a >= 128 || lower_b >= 128) {
        return false;
    }
    const char ia = static_cast<char>(lower_a);
    const char ib = static_cast<char>(lower_b);
    for (const std::string &row : keyboard_rows()) {
        const size_t index = row.find(ia);
        if (index == std::string::npos) {
            continue;
        }
        if (index > 0 && row[index - 1] == ib) {
            return true;
        }
        if (index + 1 < row.size() && row[index + 1] == ib) {
            return true;
        }
    }
    return false;
}

std::string ascii_view(const std::vector<char32_t> &chars) {
    std::string out;
    out.reserve(chars.size());
    for (char32_t cp : chars) {
        out.push_back(cp < 128 ? static_cast<char>(cp) : '?');
    }
    return out;
}

std::optional<Pattern> pattern_at(const std::vector<char32_t> &chars,
                                  const std::array<std::vector<char32_t>, 2> &swapped,
                                  const std::string &ascii, size_t index) {
    const size_t date = date_length_at(ascii, index);
    if (date != 0) {
        return Pattern{date, 2.0};
    }

    for (const std::string &word : common_words()) {
        if (index + word.size() > chars.size()) {
            continue;
        }
        for (const std::vector<char32_t> &variant : swapped) {
            bool matches = true;
            for (size_t j = 0; j < word.size(); ++j) {
                if (variant[index + j] != static_cast<char32_t>(word[j])) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                return Pattern{word.size(), 1.0};
            }
        }
    }

    const size_t max_size = std::min(chars.size() - index, index);
    for (size_t size = max_size; size >= 3; --size) {
        for (size_t start = 0; start + size <= index; ++start) {
            bool matches = true;
            for (size_t j = 0; j < size; ++j) {
                if (chars[start + j] != chars[index + j]) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                return Pattern{size, 1.0};
            }
        }
    }

    return std::nullopt;
}

double effective_length(const std::vector<char32_t> &chars) {
    std::array<std::vector<char32_t>, 2> swapped;
    for (size_t variant = 0; variant < swapped.size(); ++variant) {
        const char32_t one = variant == 0 ? 'i' : 'l';
        swapped[variant].reserve(chars.size());
        for (char32_t cp : chars) {
            swapped[variant].push_back(cp == '1' ? one : leet(cp));
        }
    }
    const std::string ascii = ascii_view(chars);

    double length = 0.0;
    char32_t previous = 0;
    bool has_previous = false;
    int run_length = 0;
    size_t i = 0;
    while (i < chars.size()) {
        const std::optional<Pattern> pattern = pattern_at(chars, swapped, ascii, i);
        if (pattern.has_value()) {
            length += pattern->weight;
            has_previous = false;
            i += pattern->length;
            continue;
        }

        const char32_t current = chars[i];
        if (has_previous && (current == previous || is_step(previous, current))) {
            ++run_length;
            if (run_length == 2) {
                length += 0.5;
            }
        } else {
            run_length = 1;
            length += 1.0;
        }
        previous = current;
        has_previous = true;
        ++i;
    }
    return length;
}

bool is_repeated_520_1314(const std::string &text) {
    size_t i = 0;
    bool repeated = false;
    while (i < text.size()) {
        if (text.compare(i, 3, "520") == 0) {
            i += 3;
            repeated = true;
        } else if (text.compare(i, 4, "1314") == 0) {
            i += 4;
            repeated = true;
        } else {
            break;
        }
    }
    if (!repeated) {
        return false;
    }
    size_t digits = 0;
    while (i < text.size() && is_ascii_digit(text[i]) && digits < 4) {
        ++i;
        ++digits;
    }
    return i == text.size();
}

std::string strip_leading_symbols(const std::string &text) {
    size_t start = 0;
    while (start < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[start]);
        if (c > 0x7E || !is_symbol_cp(c) && !is_ascii_digit(static_cast<char>(c))) {
            break;
        }
        ++start;
    }
    return text.substr(start);
}

std::string strip_trailing_symbols(const std::string &text) {
    size_t end = text.size();
    while (end > 0) {
        const unsigned char c = static_cast<unsigned char>(text[end - 1]);
        if (c > 0x7E || !is_symbol_cp(c) && !is_ascii_digit(static_cast<char>(c))) {
            break;
        }
        --end;
    }
    return text.substr(0, end);
}

} // namespace

EntropyEstimate estimate_entropy(const std::string &password) {
    EntropyEstimate estimate{0.0, 0.0, 0, 0};
    if (password.empty()) {
        return estimate;
    }

    const std::vector<char32_t> code_points = decode_utf8(password);
    const Charset charset = analyze_charset(code_points);
    estimate.code_points = static_cast<int>(code_points.size());
    estimate.charset_size = charset_size(charset);
    if (estimate.charset_size >= 2) {
        estimate.uniform_bits =
            static_cast<double>(estimate.code_points) * std::log2(estimate.charset_size);
    }

    const std::string lower = ascii_lower(password);
    const std::string core = strip_leading_symbols(strip_trailing_symbols(lower));
    const std::vector<char32_t> lower_code_points = decode_utf8(lower);

    double guess_bits = 0.0;
    if (common_passwords().count(lower) != 0 ||
        (!core.empty() && common_passwords().count(core) != 0 &&
         lower.size() - core.size() <= 6)) {
        guess_bits = 10.0;
    } else if (date_length_at(lower, 0) == lower.size() || is_repeated_520_1314(lower)) {
        guess_bits = 10.0;
    } else {
        guess_bits = effective_length(lower_code_points) * std::log2(pool_size(charset));
    }
    estimate.guess_bits = guess_bits;
    return estimate;
}

bool meets_master_password_floor(const EntropyEstimate &estimate) {
    return estimate.guess_bits >= kMasterPasswordFloorBits &&
           estimate.uniform_bits >= kMasterPasswordFloorBits;
}

double empirical_shannon_bits(const std::string &password) {
    const std::vector<char32_t> code_points = decode_utf8(password);
    if (code_points.empty()) {
        return 0.0;
    }
    std::map<char32_t, int> counts;
    for (char32_t cp : code_points) {
        ++counts[cp];
    }
    const double total = static_cast<double>(code_points.size());
    double bits = 0.0;
    for (const auto &entry : counts) {
        const double count = static_cast<double>(entry.second);
        bits -= count * std::log2(count / total);
    }
    return bits;
}
