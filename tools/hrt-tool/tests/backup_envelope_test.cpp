#include "backup_envelope.h"
#include "error.h"
#include "test_framework.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using nlohmann::json;

namespace {

json sync_payload() {
    return json{
        {"meta", {{"version", 2}, {"exportedAt", "2026-01-01T00:00:00.000Z"}}},
        {"mode", "transfem"},
        {"modes",
         {{"transfem",
           {{"events",
             json::array({{{"id", "e1"},
                           {"route", "injection"},
                           {"ester", "EV"},
                           {"doseMG", 5},
                           {"timeH", 100},
                           {"extras", json::object()},
                           {"updatedAt", 1000}}})},
            {"labResults", json::array()},
            {"doseTemplates", json::array()},
            {"deletions",
             {{"events", json::object()},
              {"labResults", json::object()},
              {"doseTemplates", json::object()}}}}},
          {"transmasc",
           {{"events", json::array()},
            {"labResults", json::array()},
            {"doseTemplates", json::array()},
            {"deletions",
             {{"events", json::object()},
              {"labResults", json::object()},
              {"doseTemplates", json::object()}}}}}}},
        {"weight", 70},
        {"weightUpdatedAt", 1000},
        {"pkParams", nullptr},
        {"pkParamsUpdatedAt", 1000},
    };
}

bool contains(const std::vector<std::string> &values, const std::string &needle) {
    return std::find(values.begin(), values.end(), needle) != values.end();
}

} // namespace

int main() {
    const json payload = sync_payload();
    const json envelope =
        export_complete_envelope(payload, "2026-10-20T00:00:00.000Z");

    CHECK_EQ(envelope.at("format").get<std::string>(),
             std::string("oyama-hrt-complete-backup"));
    CHECK_EQ(envelope.at("formatVersion").get<int>(), 1);
    CHECK_EQ(envelope.at("createdAt").get<std::string>(),
             std::string("2026-10-20T00:00:00.000Z"));
    CHECK_EQ(envelope.at("mode").get<std::string>(), std::string("transfem"));
    CHECK(envelope.contains("modes"));
    CHECK_EQ(envelope.at("modes").at("transfem").at("events").size(),
             static_cast<size_t>(1));
    CHECK(!envelope.at("modes").at("transfem").contains("quickDoses"));
    CHECK(!envelope.at("modes").at("transfem").contains("supplies"));
    CHECK(!envelope.at("modes").at("transfem").contains("plan"));
    CHECK(!envelope.contains("preferences"));
    CHECK(!envelope.contains("calibration"));
    CHECK_EQ(envelope.at("weight").get<double>(), 70.0);
    CHECK(envelope.at("pkParams").is_null());

    json full = envelope;
    full["modes"]["transfem"]["quickDoses"] = json::array({json{{"id", "q1"}}});
    full["modes"]["transfem"]["supplies"] = json::array({json{{"id", "s1"}}});
    full["modes"]["transfem"]["plan"] = json::array({json{{"id", "p1"}}});
    full["preferences"] = json{{"app-theme", "dark"}};
    full["calibration"] = json{{"method", "mipd"}};

    const EnvelopeImport imported = import_complete_envelope(full);
    CHECK_EQ(imported.sync_payload.at("meta").at("version").get<int>(), 2);
    CHECK_EQ(imported.sync_payload.at("modes").at("transfem").at("events").size(),
             static_cast<size_t>(1));
    CHECK_EQ(imported.sync_payload.at("modes")
                 .at("transfem")
                 .at("events")
                 .at(0)
                 .at("id")
                 .get<std::string>(),
             std::string("e1"));
    CHECK_EQ(imported.sync_payload.at("weight").get<double>(), 70.0);
    CHECK(imported.sync_payload.at("pkParams").is_null());
    CHECK(contains(imported.not_carried, "quickDoses"));
    CHECK(contains(imported.not_carried, "supplies"));
    CHECK(contains(imported.not_carried, "plan"));
    CHECK(contains(imported.not_carried, "preferences"));
    CHECK(contains(imported.not_carried, "calibration"));

    {
        json invalid = full;
        invalid["format"] = "wrong";
        CHECK_THROWS(import_complete_envelope(invalid), ToolError);
    }
    {
        json invalid = full;
        invalid["formatVersion"] = 2;
        CHECK_THROWS(import_complete_envelope(invalid), ToolError);
    }
    {
        json invalid = full;
        invalid["mode"] = "other";
        CHECK_THROWS(import_complete_envelope(invalid), ToolError);
    }
    {
        json invalid = full;
        invalid["modes"] = json::array();
        CHECK_THROWS(import_complete_envelope(invalid), ToolError);
    }

    return test_result();
}
