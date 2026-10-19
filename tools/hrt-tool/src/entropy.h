#pragma once

#include <string>

struct EntropyEstimate {
    double guess_bits;
    double uniform_bits;
    int code_points;
    int charset_size;
};

EntropyEstimate estimate_entropy(const std::string &password);

bool meets_master_password_floor(const EntropyEstimate &estimate);

/* Diagnostic only: empirical Shannon entropy of this one string. */
double empirical_shannon_bits(const std::string &password);
