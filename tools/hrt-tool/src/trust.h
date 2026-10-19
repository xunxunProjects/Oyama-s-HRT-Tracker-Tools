#pragma once

#include <nlohmann/json.hpp>
#include <string>

enum class TrustState { Unknown, Trusted, Changed };

class TrustStore {
public:
    explicit TrustStore(nlohmann::json &store);

    TrustState check(const std::string &origin, const std::string &spki_sha256) const;
    void trust(const std::string &origin, const std::string &spki_sha256);

private:
    nlohmann::json &store_;
};
