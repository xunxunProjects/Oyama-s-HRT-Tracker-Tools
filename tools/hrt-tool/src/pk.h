#pragma once

#include "hrtcore/pk.h"

#include <array>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

struct PkEvent {
    std::string id;
    int route = HRTCORE_ROUTE_ORAL;
    int ester = HRTCORE_ESTER_E2;
    double dose_mg = 0.0;
    double time_h = 0.0;
    uint32_t extras_present = 0;
    std::array<double, HRTCORE_EXTRA_COUNT> extras{};
};

struct PkSample {
    double e2 = 0.0;
    double cpa = 0.0;
    double t = 0.0;
};

class PkResult {
public:
    PkResult() = default;
    PkResult(PkResult &&other) noexcept;
    PkResult &operator=(PkResult &&other) noexcept;
    PkResult(const PkResult &) = delete;
    PkResult &operator=(const PkResult &) = delete;
    ~PkResult();

    const hrtcore_pk_result &raw() const { return result_; }

private:
    friend std::optional<PkResult> run_pk(const std::vector<PkEvent> &events,
                                          double weight_kg,
                                          const hrtcore_pk_params &params,
                                          double now_h);
    explicit PkResult(hrtcore_pk_result result) : result_(result) {}

    hrtcore_pk_result result_{};
};

hrtcore_pk_params default_pk_params();

/* Ports sanitizePKParams/PK_PARAM_RANGES; unknown or non-finite values are
 * dropped back to the defaults. A null/absent blob returns the defaults. */
hrtcore_pk_params sanitize_pk_params(const nlohmann::json &raw);

std::optional<PkResult> run_pk(const std::vector<PkEvent> &events, double weight_kg,
                               const hrtcore_pk_params &params, double now_h);

PkSample interpolate(const PkResult &result, double hour);

PkEvent pk_event_from_json(const nlohmann::json &event);

int route_from_string(const std::string &route);
int ester_from_string(const std::string &ester);
