#pragma once

#include <cstddef>
#include <string>

/* A master password longer than this is refused before the estimator runs. */
constexpr size_t kMaxMasterPasswordBytes = 256;

struct EntropyEstimate {
    double guess_bits = 0.0;
    double uniform_bits = 0.0;
    int code_points = 0;
    int charset_size = 0;
    bool weak_structure = false;
    std::string structure_reason;
    bool has_lower = false;
    bool has_upper = false;
    bool has_digits = false;
    bool has_symbols = false;
    bool has_non_ascii = false;
};

EntropyEstimate estimate_entropy(const std::string &password);

bool meets_master_password_floor(const EntropyEstimate &estimate);

/* Diagnostic only: empirical Shannon entropy of this one string. */
double empirical_shannon_bits(const std::string &password);
