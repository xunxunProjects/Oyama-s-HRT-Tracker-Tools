#include "test_framework.h"
#include "trust.h"

#include <nlohmann/json.hpp>

int main() {
    nlohmann::json store = nlohmann::json::object();
    TrustStore trust(store);

    CHECK(trust.check("https://example.com", "spki-a") == TrustState::Unknown);
    trust.trust("https://example.com", "spki-a");
    CHECK(trust.check("https://example.com", "spki-a") == TrustState::Trusted);
    CHECK(trust.check("https://example.com", "spki-b") == TrustState::Changed);

    trust.trust("https://example.com", "spki-b");
    CHECK(trust.check("https://example.com", "spki-b") == TrustState::Trusted);

    CHECK(trust.check("https://other.example", "spki-a") == TrustState::Unknown);
    CHECK(store.contains("https://example.com"));

    return test_result();
}
