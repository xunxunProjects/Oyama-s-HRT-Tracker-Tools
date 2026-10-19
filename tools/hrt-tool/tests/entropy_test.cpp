#include "entropy.h"
#include "test_framework.h"

#include <cmath>

int main() {
    const char *weak[] = {"password", "aaaaaaaaaaaaaaaa", "qwertyuiop", "12345678",
                          "Password123!"};
    for (const char *candidate : weak) {
        const EntropyEstimate estimate = estimate_entropy(candidate);
        if (!(estimate.guess_bits < 40.0)) {
            std::cerr << "expected weak: " << candidate << " (" << estimate.guess_bits
                      << " bits)\n";
            ++g_failures;
        }
        CHECK(!meets_master_password_floor(estimate));
    }

    const char *structural_weak[] = {
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1!",
        "1q2w3e4r5t6y7u8i9o0p",
        "qazwsxedcrfvtgbyhnujmikolp",
        "xxxxxxxxxxxxxxxxxxxxA1!",
        "abcabcabcabcabc",
        "123123123123123",
    };
    for (const char *candidate : structural_weak) {
        const EntropyEstimate estimate = estimate_entropy(candidate);
        if (meets_master_password_floor(estimate)) {
            std::cerr << "expected structurally weak: " << candidate << " (guess "
                      << estimate.guess_bits << ", uniform " << estimate.uniform_bits
                      << ")\n";
            ++g_failures;
        }
    }

    const EntropyEstimate passphrase = estimate_entropy("correct horse battery staple");
    CHECK(passphrase.guess_bits >= 40.0);
    CHECK(passphrase.uniform_bits >= 40.0);
    CHECK(meets_master_password_floor(passphrase));

    const EntropyEstimate randomish = estimate_entropy("aB3dE6gH9jK2");
    CHECK(randomish.guess_bits >= 40.0);
    CHECK(randomish.uniform_bits >= 40.0);
    CHECK(meets_master_password_floor(randomish));

    const EntropyEstimate alphabet = estimate_entropy("abcdefghijkl");
    CHECK_NEAR(alphabet.uniform_bits, 12.0 * std::log2(26.0), 1e-9);

    const EntropyEstimate cjk = estimate_entropy("密码密码");
    CHECK_EQ(cjk.code_points, 4);
    CHECK(cjk.charset_size >= 100);

    CHECK_NEAR(empirical_shannon_bits("ab"), 2.0, 1e-9);
    CHECK_NEAR(empirical_shannon_bits("aaaa"), 0.0, 1e-9);

    return test_result();
}
