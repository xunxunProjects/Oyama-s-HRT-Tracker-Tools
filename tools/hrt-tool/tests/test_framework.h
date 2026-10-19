#pragma once

#include <cstdint>
#include <iostream>
#include <string>

static int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond     \
                      << "\n";                                                      \
            ++g_failures;                                                           \
        }                                                                           \
    } while (0)

#define CHECK_EQ(actual, expected)                                                  \
    do {                                                                            \
        const auto actual_value = (actual);                                         \
        const auto expected_value = (expected);                                     \
        if (!(actual_value == expected_value)) {                                    \
            std::cerr << __FILE__ << ":" << __LINE__ << " CHECK_EQ failed: "       \
                      << #actual << " == " << #expected << "\n";                    \
            ++g_failures;                                                           \
        }                                                                           \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                     \
    do {                                                                            \
        const double actual_value = (actual);                                       \
        const double expected_value = (expected);                                   \
        const double tolerance_value = (tolerance);                                 \
        const double diff = actual_value > expected_value                           \
                                ? actual_value - expected_value                     \
                                : expected_value - actual_value;                    \
        if (!(diff <= tolerance_value)) {                                           \
            std::cerr << __FILE__ << ":" << __LINE__ << " CHECK_NEAR failed: "      \
                      << actual_value << " vs " << expected_value << "\n";          \
            ++g_failures;                                                           \
        }                                                                           \
    } while (0)

inline std::string hex_encode(const uint8_t *data, size_t len) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0f]);
    }
    return out;
}

inline std::string hex_encode(const std::string &bytes) {
    return hex_encode(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
}

inline bool hex_decode(const std::string &hex, uint8_t *out, size_t out_cap, size_t *out_len) {
    if (hex.size() % 2 != 0 || hex.size() / 2 > out_cap) {
        return false;
    }
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i / 2] = static_cast<uint8_t>((hi << 4) | lo);
    }
    *out_len = hex.size() / 2;
    return true;
}

#define CHECK_HEX(actual, len, expected_hex)                                        \
    do {                                                                            \
        const std::string actual_hex = hex_encode((actual), (len));                 \
        const std::string expected_hex_value = (expected_hex);                      \
        if (actual_hex != expected_hex_value) {                                     \
            std::cerr << __FILE__ << ":" << __LINE__ << " CHECK_HEX failed:\n"     \
                      << "  actual:   " << actual_hex << "\n"                       \
                      << "  expected: " << expected_hex_value << "\n";              \
            ++g_failures;                                                           \
        }                                                                           \
    } while (0)

inline int test_result() {
    if (g_failures == 0) {
        std::cout << "PASS\n";
        return 0;
    }
    std::cerr << g_failures << " failure(s)\n";
    return 1;
}
