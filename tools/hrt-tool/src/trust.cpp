#include "trust.h"

#include <chrono>

namespace {

int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

TrustStore::TrustStore(nlohmann::json &store) : store_(store) {}

TrustState TrustStore::check(const std::string &origin,
                             const std::string &spki_sha256) const {
    if (!store_.is_object()) {
        return TrustState::Unknown;
    }
    const auto entry = store_.find(origin);
    if (entry == store_.end() || !entry->is_object()) {
        return TrustState::Unknown;
    }
    const std::string stored = entry->value("spkiSha256", std::string{});
    return stored == spki_sha256 ? TrustState::Trusted : TrustState::Changed;
}

void TrustStore::trust(const std::string &origin, const std::string &spki_sha256) {
    if (!store_.is_object()) {
        store_ = nlohmann::json::object();
    }
    nlohmann::json entry = store_.value(origin, nlohmann::json::object());
    if (!entry.is_object()) {
        entry = nlohmann::json::object();
    }
    if (!entry.contains("firstSeen")) {
        entry["firstSeen"] = now_seconds();
    }
    entry["lastSeen"] = now_seconds();
    entry["spkiSha256"] = spki_sha256;
    store_[origin] = entry;
}
