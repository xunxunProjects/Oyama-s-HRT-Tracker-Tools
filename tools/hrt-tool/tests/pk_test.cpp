#include "error.h"
#include "pk.h"
#include "test_framework.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

#ifndef HRT_TOOL_TESTDATA_DIR
#error "HRT_TOOL_TESTDATA_DIR must be defined"
#endif

namespace {

void check_close(const std::string &label, double actual, double expected) {
    if (!std::isfinite(actual) || !std::isfinite(expected)) {
        std::cerr << label << ": non-finite value (actual " << actual << ", expected "
                  << expected << ")\n";
        ++g_failures;
        return;
    }
    const double scale = std::max(std::fabs(actual), std::fabs(expected));
    const double tolerance = std::max(1e-9, 1e-7 * scale);
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << label << ": actual " << actual << ", expected " << expected
                  << ", diff " << std::fabs(actual - expected) << "\n";
        ++g_failures;
    }
}

} // namespace

int main() {
    std::ifstream input(std::string(HRT_TOOL_TESTDATA_DIR) + "/pk-vectors.json");
    if (!input) {
        std::cerr << "cannot open pk-vectors.json in " << HRT_TOOL_TESTDATA_DIR << "\n";
        return 1;
    }
    nlohmann::json vectors;
    input >> vectors;
    if (!vectors.contains("vectors") || !vectors.at("vectors").is_array()) {
        std::cerr << "pk-vectors.json has no vectors array\n";
        return 1;
    }

    size_t scenarios_run = 0;
    for (const nlohmann::json &scenario : vectors.at("vectors")) {
        const std::string name = scenario.at("name").get<std::string>();
        const nlohmann::json params_json =
            scenario.contains("params") ? scenario.at("params") : nlohmann::json();
        const hrtcore_pk_params params = sanitize_pk_params(params_json);

        std::vector<PkEvent> events;
        for (const nlohmann::json &event : scenario.at("events")) {
            events.push_back(pk_event_from_json(event));
        }

        const double now_h = scenario.at("nowH").get<double>();
        const double weight = scenario.at("weight").get<double>();

        std::optional<PkResult> result;
        try {
            result = run_pk(events, weight, params, now_h);
        } catch (const ToolError &error) {
            std::cerr << name << ": run_pk threw: " << error.what() << " ["
                      << error.code() << "]\n";
            ++g_failures;
            continue;
        } catch (const std::exception &error) {
            std::cerr << name << ": run_pk threw: " << error.what() << "\n";
            ++g_failures;
            continue;
        }
        if (!result.has_value()) {
            std::cerr << name << ": run_pk returned no result\n";
            ++g_failures;
            continue;
        }
        ++scenarios_run;

        const size_t expected_length = scenario.at("gridLength").get<size_t>();
        if (result->raw().length != expected_length) {
            std::cerr << name << ": grid length " << result->raw().length << ", expected "
                      << expected_length << "\n";
            ++g_failures;
            continue;
        }
        check_close(name + ".auc", result->raw().auc, scenario.at("auc").get<double>());

        for (const nlohmann::json &grid : scenario.at("grid")) {
            const size_t index = grid.at("index").get<size_t>();
            check_close(name + ".grid.time", result->raw().time_h[index],
                        grid.at("timeH").get<double>());
            check_close(name + ".grid.e2", result->raw().e2[index],
                        grid.at("e2").get<double>());
            check_close(name + ".grid.cpa", result->raw().cpa[index],
                        grid.at("cpa").get<double>());
            check_close(name + ".grid.t", result->raw().t[index],
                        grid.at("t").get<double>());
        }

        for (const nlohmann::json &sample : scenario.at("sample")) {
            const double offset = sample.at("offset").get<double>();
            const PkSample actual = interpolate(*result, now_h + offset);
            check_close(name + ".sample.e2@" + std::to_string(offset), actual.e2,
                        sample.at("e2").get<double>());
            check_close(name + ".sample.cpa@" + std::to_string(offset), actual.cpa,
                        sample.at("cpa").get<double>());
            check_close(name + ".sample.t@" + std::to_string(offset), actual.t,
                        sample.at("t").get<double>());
        }
    }

    CHECK_EQ(scenarios_run, vectors.at("vectors").size());

    // Age clearance adjustment, frozen from logic.ts at the same nowH. The
    // vectors carry a birth year and full-precision samples, so this pins the
    // slope, the 19..75 clamp, and the per-dose evaluation time.
    {
        std::ifstream age_input(std::string(HRT_TOOL_TESTDATA_DIR) +
                                "/pk-age-vectors.json");
        if (!age_input) {
            std::cerr << "cannot open pk-age-vectors.json in "
                      << HRT_TOOL_TESTDATA_DIR << "\n";
            ++g_failures;
        } else {
            nlohmann::json age_vectors;
            age_input >> age_vectors;
            size_t age_scenarios = 0;
            for (const nlohmann::json &scenario : age_vectors.at("vectors")) {
                const std::string name =
                    scenario.at("name").get<std::string>();
                const double now_h =
                    scenario.value("nowH", age_vectors.at("nowH").get<double>());
                const double weight = scenario.at("weight").get<double>();
                const int birth_year =
                    scenario.at("birthYear").get<int>();

                std::vector<PkEvent> events;
                for (const nlohmann::json &event : scenario.at("events")) {
                    events.push_back(pk_event_from_json(event));
                }
                std::optional<PkResult> result;
                try {
                    result = run_pk(events, weight, default_pk_params(), now_h,
                                    birth_year);
                } catch (const ToolError &error) {
                    std::cerr << name << ": run_pk threw: " << error.what()
                              << "\n";
                    ++g_failures;
                    continue;
                }
                if (!result.has_value()) {
                    std::cerr << name << ": run_pk returned no result\n";
                    ++g_failures;
                    continue;
                }
                ++age_scenarios;

                check_close(name + ".auc", result->raw().auc,
                            scenario.at("auc").get<double>());
                double auc_t = 0.0;
                for (size_t i = 1; i < result->raw().length; ++i) {
                    auc_t += 0.5 *
                             (result->raw().t[i] + result->raw().t[i - 1]) *
                             (result->raw().time_h[i] -
                              result->raw().time_h[i - 1]);
                }
                check_close(name + ".aucT", auc_t,
                            scenario.at("aucT").get<double>());

                for (const nlohmann::json &sample :
                     scenario.at("samples")) {
                    const double hour = sample.at("timeH").get<double>();
                    const PkSample actual = interpolate(*result, hour);
                    const std::string at = "@" + std::to_string(hour);
                    check_close(name + ".e2" + at, actual.e2,
                                sample.at("e2").get<double>());
                    check_close(name + ".cpa" + at, actual.cpa,
                                sample.at("cpa").get<double>());
                    check_close(name + ".t" + at, actual.t,
                                sample.at("t").get<double>());
                }
            }
            CHECK_EQ(age_scenarios, age_vectors.at("vectors").size());
        }
    }

    // The C ABI rejects non-finite and out-of-range inputs instead of
    // producing NaN concentrations or absurd doses.
    {
        const hrtcore_pk_params params = default_pk_params();
        PkEvent event;
        event.id = "bounds";
        event.route = HRTCORE_ROUTE_ORAL;
        event.ester = HRTCORE_ESTER_E2;
        event.dose_mg = 2.0;
        event.time_h = 479800.0;

        const double nan = std::nan("");
        CHECK_THROWS(run_pk({event}, nan, params, 480000.0), ToolError);
        CHECK_THROWS(run_pk({event}, 10.0, params, 480000.0), ToolError);
        CHECK_THROWS(run_pk({event}, 401.0, params, 480000.0), ToolError);

        PkEvent bad_dose = event;
        bad_dose.dose_mg = nan;
        CHECK_THROWS(run_pk({bad_dose}, 70.0, params, 480000.0), ToolError);
        bad_dose.dose_mg = -1.0;
        CHECK_THROWS(run_pk({bad_dose}, 70.0, params, 480000.0), ToolError);
        bad_dose.dose_mg = 10001.0;
        CHECK_THROWS(run_pk({bad_dose}, 70.0, params, 480000.0), ToolError);

        PkEvent bad_time = event;
        bad_time.time_h = nan;
        CHECK_THROWS(run_pk({bad_time}, 70.0, params, 480000.0), ToolError);
        bad_time.time_h = -1.0;
        CHECK_THROWS(run_pk({bad_time}, 70.0, params, 480000.0), ToolError);
        bad_time.time_h = 1.6e6;
        CHECK_THROWS(run_pk({bad_time}, 70.0, params, 480000.0), ToolError);

        PkEvent bad_extra = event;
        bad_extra.extras_present = 1u << HRTCORE_EXTRA_PATCH_WEAR_H;
        bad_extra.extras[HRTCORE_EXTRA_PATCH_WEAR_H] = nan;
        CHECK_THROWS(run_pk({bad_extra}, 70.0, params, 480000.0), ToolError);
    }

    return test_result();
}
