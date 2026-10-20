#include "pk.h"

#include "error.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

namespace {

struct ParamSpec {
    const char *key;
    double hrtcore_pk_params::*member;
    double minimum;
    double maximum;
};

const ParamSpec kParamSpecs[] = {
    {"e2_kClear", &hrtcore_pk_params::e2_kClear, 0.001, 5.0},
    {"e2_kClearInj", &hrtcore_pk_params::e2_kClearInj, 0.001, 1.0},
    {"e2_ff_EB", &hrtcore_pk_params::e2_ff_EB, 0.0, 1.0},
    {"e2_ff_EV", &hrtcore_pk_params::e2_ff_EV, 0.0, 1.0},
    {"e2_ff_EC", &hrtcore_pk_params::e2_ff_EC, 0.0, 1.0},
    {"e2_ff_EN", &hrtcore_pk_params::e2_ff_EN, 0.0, 1.0},
    {"e2_ff_EU", &hrtcore_pk_params::e2_ff_EU, 0.0, 1.0},
    {"e2_oral_bio", &hrtcore_pk_params::e2_oral_bio, 0.0, 1.0},
    {"e2_sl_quick", &hrtcore_pk_params::e2_sl_quick, 0.0, 1.0},
    {"e2_sl_casual", &hrtcore_pk_params::e2_sl_casual, 0.0, 1.0},
    {"e2_sl_standard", &hrtcore_pk_params::e2_sl_standard, 0.0, 1.0},
    {"e2_sl_strict", &hrtcore_pk_params::e2_sl_strict, 0.0, 1.0},
    {"e2_gel_arm", &hrtcore_pk_params::e2_gel_arm, 0.0, 1.0},
    {"e2_gel_thigh", &hrtcore_pk_params::e2_gel_thigh, 0.0, 1.0},
    {"e2_gel_scrotal", &hrtcore_pk_params::e2_gel_scrotal, 0.0, 1.0},
    {"t_kClear", &hrtcore_pk_params::t_kClear, 0.001, 5.0},
    {"t_kClearInj", &hrtcore_pk_params::t_kClearInj, 0.001, 1.0},
    {"t_ff_TC", &hrtcore_pk_params::t_ff_TC, 0.0, 1.0},
    {"t_ff_TE", &hrtcore_pk_params::t_ff_TE, 0.0, 1.0},
    {"t_ff_TU", &hrtcore_pk_params::t_ff_TU, 0.0, 1.0},
    {"t_gel_arm", &hrtcore_pk_params::t_gel_arm, 0.0, 1.0},
    {"t_gel_thigh", &hrtcore_pk_params::t_gel_thigh, 0.0, 1.0},
    {"t_gel_scrotal", &hrtcore_pk_params::t_gel_scrotal, 0.0, 1.0},
};

bool is_former_default(const std::string &key, double value) {
    return (key == "t_ff_TC" || key == "t_ff_TE" || key == "t_ff_TU") &&
           value == 0.025;
}

bool read_number(const nlohmann::json &value, double *out) {
    if (value.is_number()) {
        *out = value.get<double>();
        return std::isfinite(*out);
    }
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        if (text.empty()) {
            return false;
        }
        char *end = nullptr;
        const double parsed = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0' || !std::isfinite(parsed)) {
            return false;
        }
        *out = parsed;
        return true;
    }
    return false;
}

bool is_unmodelled(int ester) {
    return ester < 0 || ester == HRTCORE_ESTER_SPIRO || ester == HRTCORE_ESTER_BICA ||
           ester == HRTCORE_ESTER_MPA || ester == HRTCORE_ESTER_LEUP ||
           ester == HRTCORE_ESTER_TRIP || ester == HRTCORE_ESTER_RLX;
}

} // namespace

int route_from_string(const std::string &route) {
    static const std::map<std::string, int> kRoutes = {
        {"sublingual", HRTCORE_ROUTE_SUBLINGUAL},
        {"injection", HRTCORE_ROUTE_INJECTION},
        {"patchApply", HRTCORE_ROUTE_PATCH_APPLY},
        {"patchRemove", HRTCORE_ROUTE_PATCH_REMOVE},
        {"gel", HRTCORE_ROUTE_GEL},
        {"oral", HRTCORE_ROUTE_ORAL},
    };
    const auto found = kRoutes.find(route);
    if (found == kRoutes.end()) {
        throw ToolError(ExitCode::Data, "PK_ROUTE", "unknown route: " + route);
    }
    return found->second;
}

int ester_from_string(const std::string &ester) {
    static const std::map<std::string, int> kEsters = {
        {"E2", HRTCORE_ESTER_E2},     {"EB", HRTCORE_ESTER_EB},
        {"EV", HRTCORE_ESTER_EV},     {"EC", HRTCORE_ESTER_EC},
        {"EN", HRTCORE_ESTER_EN},     {"EU", HRTCORE_ESTER_EU},
        {"CPA", HRTCORE_ESTER_CPA},   {"SPIRO", HRTCORE_ESTER_SPIRO},
        {"BICA", HRTCORE_ESTER_BICA}, {"MPA", HRTCORE_ESTER_MPA},
        {"LEUP", HRTCORE_ESTER_LEUP}, {"TRIP", HRTCORE_ESTER_TRIP},
        {"RLX", HRTCORE_ESTER_RLX},   {"T", HRTCORE_ESTER_T},
        {"TC", HRTCORE_ESTER_TC},     {"TE", HRTCORE_ESTER_TE},
        {"TU", HRTCORE_ESTER_TU},
    };
    const auto found = kEsters.find(ester);
    return found == kEsters.end() ? -1 : found->second;
}

hrtcore_pk_params default_pk_params() {
    hrtcore_pk_params params{};
    hrtcore_pk_defaults(&params);
    return params;
}

hrtcore_pk_params sanitize_pk_params(const nlohmann::json &raw) {
    hrtcore_pk_params params = default_pk_params();
    if (!raw.is_object()) {
        return params;
    }
    for (const ParamSpec &spec : kParamSpecs) {
        if (!raw.contains(spec.key)) {
            continue;
        }
        double value = 0.0;
        if (!read_number(raw.at(spec.key), &value)) {
            continue;
        }
        if (is_former_default(spec.key, value)) {
            continue;
        }
        params.*spec.member = std::min(spec.maximum, std::max(spec.minimum, value));
    }
    return params;
}

PkEvent pk_event_from_json(const nlohmann::json &event) {
    PkEvent out;
    out.id = event.value("id", std::string{});
    out.route = route_from_string(event.value("route", std::string{}));
    out.ester = ester_from_string(event.value("ester", std::string{}));
    out.dose_mg = event.value("doseMG", 0.0);
    out.time_h = event.value("timeH", 0.0);

    if (event.contains("extras") && event.at("extras").is_object()) {
        const nlohmann::json &extras = event.at("extras");
        const auto set_extra = [&](const char *key, int index) {
            if (!extras.contains(key)) {
                return;
            }
            double value = 0.0;
            if (read_number(extras.at(key), &value)) {
                out.extras[static_cast<size_t>(index)] = value;
                out.extras_present |= (1u << index);
            }
        };
        set_extra("concentrationMGmL", HRTCORE_EXTRA_CONCENTRATION_MG_ML);
        set_extra("areaCM2", HRTCORE_EXTRA_AREA_CM2);
        set_extra("releaseRateUGPerDay", HRTCORE_EXTRA_RELEASE_RATE_UG_PER_DAY);
        set_extra("sublingualTheta", HRTCORE_EXTRA_SUBLINGUAL_THETA);
        set_extra("sublingualTier", HRTCORE_EXTRA_SUBLINGUAL_TIER);
        set_extra("gelSite", HRTCORE_EXTRA_GEL_SITE);
        set_extra("patchWearH", HRTCORE_EXTRA_PATCH_WEAR_H);
    }
    return out;
}

PkResult::PkResult(PkResult &&other) noexcept : result_(other.result_) {
    other.result_ = hrtcore_pk_result{};
}

PkResult &PkResult::operator=(PkResult &&other) noexcept {
    if (this != &other) {
        hrtcore_pk_result_free(&result_);
        result_ = other.result_;
        other.result_ = hrtcore_pk_result{};
    }
    return *this;
}

PkResult::~PkResult() {
    hrtcore_pk_result_free(&result_);
}

std::optional<PkResult> run_pk(const std::vector<PkEvent> &events, double weight_kg,
                               const hrtcore_pk_params &params, double now_h,
                               int birth_year) {
    std::vector<hrtcore_dose_event> converted;
    converted.reserve(events.size());
    for (const PkEvent &event : events) {
        if (is_unmodelled(event.ester)) {
            continue;
        }
        hrtcore_dose_event out{};
        std::snprintf(out.id, sizeof(out.id), "%s", event.id.c_str());
        out.route = event.route;
        out.ester = event.ester;
        out.dose_mg = event.dose_mg;
        out.time_h = event.time_h;
        out.extras_present = event.extras_present;
        std::copy(event.extras.begin(), event.extras.end(), out.extras);
        converted.push_back(out);
    }

    hrtcore_pk_result result{};
    hrtcore_error error{};
    const int status = hrtcore_pk_run(converted.data(), converted.size(), weight_kg,
                                      &params,
                                      birth_year < 1900 ? 0 : birth_year, now_h,
                                      &result, &error);
    if (status == HRTCORE_ERR_NO_DATA) {
        return std::nullopt;
    }
    if (status != HRTCORE_OK) {
        const ExitCode code = status == HRTCORE_ERR_NOT_IMPLEMENTED
                                  ? ExitCode::NotImplemented
                                  : ExitCode::Data;
        throw ToolError(code, "PK", error.message);
    }
    return PkResult(result);
}

PkSample interpolate(const PkResult &result, double hour) {
    PkSample sample;
    sample.e2 = hrtcore_pk_interp_e2(&result.raw(), hour);
    sample.cpa = hrtcore_pk_interp_cpa(&result.raw(), hour);
    sample.t = hrtcore_pk_interp_t(&result.raw(), hour);
    return sample;
}
