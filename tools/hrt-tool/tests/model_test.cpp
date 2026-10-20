#include "model.h"
#include "test_framework.h"

#include <nlohmann/json.hpp>
#include <cmath>
#include <string>

using nlohmann::json;

namespace {

json event(const std::string &id, const std::string &ester, double dose, double time,
           int64_t updated) {
    return json{{"id", id},       {"route", "oral"}, {"ester", ester},
                {"doseMG", dose}, {"timeH", time},   {"extras", json::object()},
                {"updatedAt", updated}};
}

json lab(const std::string &id, double value, const std::string &unit, int64_t updated) {
    return json{{"id", id}, {"timeH", 500.0}, {"concValue", value},
                {"unit", unit}, {"updatedAt", updated}};
}

json empty_block() {
    return json{{"events", json::array()},
                {"labResults", json::array()},
                {"doseTemplates", json::array()},
                {"deletions",
                 {{"events", json::object()},
                  {"labResults", json::object()},
                  {"doseTemplates", json::object()}}}};
}

json base_payload() {
    json payload{
        {"meta", {{"version", 2}, {"exportedAt", "2026-01-01T00:00:00.000Z"}}},
        {"mode", "transfem"},
        {"modes", {{"transfem", empty_block()}, {"transmasc", empty_block()}}},
        {"weight", 70},
        {"weightUpdatedAt", 1000},
        {"pkParams", nullptr},
        {"pkParamsUpdatedAt", 1000},
        {"x-unknown", "keep"},
    };
    payload["modes"]["transfem"]["events"].push_back(event("e1", "EV", 5, 100, 1000));
    payload["modes"]["transfem"]["labResults"].push_back(lab("l1", 180, "pg/ml", 1000));
    return payload;
}

const json *find_event(const json &payload, const std::string &mode,
                       const std::string &id) {
    for (const json &record : payload["modes"][mode]["events"]) {
        if (record.value("id", std::string{}) == id) {
            return &record;
        }
    }
    return nullptr;
}

bool has_event(const json &payload, const std::string &mode, const std::string &id) {
    return find_event(payload, mode, id) != nullptr;
}

} // namespace

int main() {
    {
        const json payload = base_payload();
        const SyncState state = normalize_sync_state(payload);
        CHECK_EQ(state.modes[0].events.size(), static_cast<size_t>(1));
        CHECK_EQ(state.modes[0].lab_results.size(), static_cast<size_t>(1));
        CHECK(state.weight.has_value());
        CHECK_EQ(*state.weight, 70.0);
        CHECK(state.pk_params.has_value());
        CHECK(state.pk_params->is_null());
        CHECK_EQ(state.weight_updated_at, static_cast<int64_t>(1000));
    }

    {
        const json flat = json{{"events", json::array({event("t1", "T", 50, 100, 1),
                                                       event("e1", "EV", 5, 100, 1)})},
                               {"labResults", json::array({lab("l1", 500, "ng/dl", 1),
                                                           lab("l2", 180, "pg/ml", 1)})},
                               {"weight", 80}};
        const SyncState state = normalize_sync_state(flat);
        CHECK_EQ(state.modes[1].events.size(), static_cast<size_t>(1));
        CHECK_EQ(state.modes[0].events.size(), static_cast<size_t>(1));
        CHECK_EQ(state.modes[1].lab_results.size(), static_cast<size_t>(1));
        CHECK_EQ(state.modes[0].lab_results.size(), static_cast<size_t>(1));
        CHECK_EQ(state.modes[1].events[0]["id"].get<std::string>(), std::string("t1"));
    }

    {
        const json naked = json::array({event("e1", "EV", 5, 100, 1)});
        const SyncState state = normalize_sync_state(naked);
        CHECK_EQ(state.modes[0].events.size(), static_cast<size_t>(1));
    }

    {
        const json local = base_payload();
        json remote = base_payload();
        remote["modes"]["transfem"]["events"].push_back(event("e2", "E2", 2, 200, 1200));
        const MergeResult result = merge_sync_payloads(local, remote);
        CHECK(!result.aborted);
        CHECK(has_event(result.payload, "transfem", "e1"));
        CHECK(has_event(result.payload, "transfem", "e2"));
        CHECK_EQ(result.stats.added, 1);
        CHECK_EQ(result.payload["x-unknown"].get<std::string>(), std::string("keep"));
    }

    {
        const json local = base_payload();
        json remote = base_payload();
        remote["modes"]["transfem"]["events"] = json::array({event("e1", "EV", 6, 100, 2000)});
        const MergeResult result = merge_sync_payloads(local, remote);
        const json *merged = find_event(result.payload, "transfem", "e1");
        CHECK(merged != nullptr);
        CHECK_EQ((*merged)["doseMG"].get<double>(), 6.0);
        CHECK_EQ(result.stats.updated, 1);
    }

    {
        const json local = base_payload();
        json remote = base_payload();
        remote["modes"]["transfem"]["events"] = json::array({event("e1", "EV", 4, 100, 500)});
        const MergeResult result = merge_sync_payloads(local, remote);
        const json *merged = find_event(result.payload, "transfem", "e1");
        CHECK(merged != nullptr);
        CHECK_EQ((*merged)["doseMG"].get<double>(), 5.0);
    }

    {
        json local = base_payload();
        json remote = base_payload();
        local["modes"]["transfem"]["events"] = json::array({event("e1", "EV", 5, 100, 1000)});
        remote["modes"]["transfem"]["events"] = json::array({event("e1", "EV", 6, 100, 1000)});
        const MergeResult forward = merge_sync_payloads(local, remote);
        const MergeResult backward = merge_sync_payloads(remote, local);
        const json *a = find_event(forward.payload, "transfem", "e1");
        const json *b = find_event(backward.payload, "transfem", "e1");
        CHECK(a != nullptr && b != nullptr);
        CHECK_EQ((*a)["doseMG"].get<double>(), (*b)["doseMG"].get<double>());
    }

    {
        json local = base_payload();
        local["modes"]["transfem"]["events"] = json::array(
            {event("dup", "EV", 5, 100, 1), event("dup", "EV", 6, 100, 2)});
        const MergeResult result = merge_sync_payloads(local, base_payload());
        CHECK(result.aborted);
        CHECK(!result.warnings.empty());
    }

    {
        const json local = base_payload();
        json remote = base_payload();
        remote["modes"]["transfem"]["deletions"]["events"]["e1"] = 2000;
        const MergeResult result = merge_sync_payloads(local, remote);
        CHECK(!has_event(result.payload, "transfem", "e1"));
        CHECK_EQ(result.stats.removed, 1);
    }

    {
        json local = base_payload();
        local["modes"]["transfem"]["deletions"]["events"]["e1"] = 2000;
        clear_tombstone(local, ModeKey::Transfem, RecordKind::Events, "e1");
        const MergeResult result = merge_sync_payloads(local, base_payload());
        CHECK(has_event(result.payload, "transfem", "e1"));
    }

    {
        const json local = base_payload();
        json remote = base_payload();
        remote["weight"] = 80;
        remote["weightUpdatedAt"] = 2000;
        const MergeResult result = merge_sync_payloads(local, remote);
        CHECK_EQ(result.payload["weight"].get<double>(), 80.0);
        CHECK_EQ(result.payload["weightUpdatedAt"].get<int64_t>(), 2000);
    }

    {
        json payload = base_payload();
        write_tombstone(payload, ModeKey::Transfem, RecordKind::Events, "e1", 3000);
        CHECK(!has_event(payload, "transfem", "e1"));
        CHECK_EQ(payload["modes"]["transfem"]["deletions"]["events"]["e1"].get<int64_t>(),
                 3000);
        clear_tombstone(payload, ModeKey::Transfem, RecordKind::Events, "e1");
        CHECK(!payload["modes"]["transfem"]["deletions"]["events"].contains("e1"));
    }

    {
        json payload = base_payload();
        const int64_t now = 1000LL * 24 * 60 * 60 * 1000;
        payload["modes"]["transfem"]["deletions"]["events"]["old"] =
            now - kTombstoneTtlMs - 1;
        payload["modes"]["transfem"]["deletions"]["events"]["recent"] = now - 1000;
        prune_tombstones(payload, now);
        CHECK(!payload["modes"]["transfem"]["deletions"]["events"].contains("old"));
        CHECK(payload["modes"]["transfem"]["deletions"]["events"].contains("recent"));
    }

    {
        CHECK(declared_mode(base_payload()) == ModeKey::Transfem);
        json other = base_payload();
        other["mode"] = "transmasc";
        CHECK(declared_mode(other) == ModeKey::Transmasc);
        json missing = base_payload();
        missing.erase("mode");
        CHECK(!declared_mode(missing).has_value());
        CHECK(active_mode(missing) == ModeKey::Transfem);
    }

    /* cleanDoseMG parity with the website (src/utils/cleanDose.ts): a value
       within 0.01% of a whole microgram snaps to it; otherwise six
       significant figures. */
    {
        CHECK_EQ(clean_dose_mg(12.499934062706513), 12.5);
        CHECK_EQ(clean_dose_mg(12.5), 12.5);
        CHECK_EQ(clean_dose_mg(0.0), 0.0);
        CHECK(std::abs(clean_dose_mg(0.0375) - 0.0375) < 1e-15);
        CHECK(std::abs(clean_dose_mg(1.23456789) - 1.23457) < 1e-12);
        CHECK(std::isinf(clean_dose_mg(INFINITY)));
    }

    {
        json payload = base_payload();
        payload["modes"]["transfem"]["events"] = json::array(
            {event("e1", "EV", 12.499934062706513, 100, 1000)});
        const json sanitized = sanitize_import_payload(payload, nullptr);
        const json *cleaned = find_event(sanitized, "transfem", "e1");
        CHECK(cleaned != nullptr);
        CHECK_EQ((*cleaned)["doseMG"].get<double>(), 12.5);
    }

    {
        json clean = base_payload();
        clean["modes"]["transfem"]["events"] =
            json::array({event("e1", "EV", 12.5, 100, 1000)});
        json dirty = base_payload();
        dirty["modes"]["transfem"]["events"] =
            json::array({event("e1", "EV", 12.499934062706513, 100, 1000)});
        CHECK_EQ(fingerprint_state(normalize_sync_state(clean)),
                 fingerprint_state(normalize_sync_state(dirty)));
    }

    /* birthYear mirrors syncMerge.ts: integer 1900..current year; present but
       implausible reads as an explicit null, absent stays unstated. */
    {
        json payload = base_payload();
        payload["birthYear"] = 1990;
        payload["birthYearUpdatedAt"] = 500;
        SyncState state = normalize_sync_state(payload);
        CHECK(state.birth_year.has_value());
        CHECK_EQ(state.birth_year->get<int64_t>(), 1990);
        CHECK_EQ(state.birth_year_updated_at, static_cast<int64_t>(500));

        for (const json &bad : {json(1800), json(3000), json(1990.5),
                                json("1990")}) {
            json invalid = base_payload();
            invalid["birthYear"] = bad;
            invalid["birthYearUpdatedAt"] = 600;
            state = normalize_sync_state(invalid);
            CHECK(state.birth_year.has_value());
            CHECK(state.birth_year->is_null());
            CHECK_EQ(state.birth_year_updated_at, static_cast<int64_t>(600));
        }

        json explicit_null = base_payload();
        explicit_null["birthYear"] = nullptr;
        explicit_null["birthYearUpdatedAt"] = 700;
        state = normalize_sync_state(explicit_null);
        CHECK(state.birth_year.has_value());
        CHECK(state.birth_year->is_null());
        CHECK_EQ(state.birth_year_updated_at, static_cast<int64_t>(700));

        state = normalize_sync_state(base_payload());
        CHECK(!state.birth_year.has_value());
        CHECK_EQ(state.birth_year_updated_at, static_cast<int64_t>(0));
    }

    {
        json payload = base_payload();
        payload["birthYear"] = nullptr;
        payload["birthYearUpdatedAt"] = 600;
        json out = to_sync_payload(normalize_sync_state(payload), json::object());
        CHECK(out.contains("birthYear"));
        CHECK(out["birthYear"].is_null());
        CHECK_EQ(out["birthYearUpdatedAt"].get<int64_t>(), 600);

        out = to_sync_payload(normalize_sync_state(base_payload()), json::object());
        CHECK(!out.contains("birthYear"));
        CHECK(!out.contains("birthYearUpdatedAt"));
    }

    {
        json local = base_payload();
        local["birthYear"] = 1990;
        local["birthYearUpdatedAt"] = 1000;
        json remote = base_payload();
        remote["birthYear"] = 1985;
        remote["birthYearUpdatedAt"] = 2000;
        MergeResult result = merge_sync_payloads(local, remote);
        CHECK_EQ(result.payload["birthYear"].get<int64_t>(), 1985);
        CHECK_EQ(result.payload["birthYearUpdatedAt"].get<int64_t>(), 2000);

        remote["birthYear"] = nullptr;
        remote["birthYearUpdatedAt"] = 3000;
        result = merge_sync_payloads(local, remote);
        CHECK(result.payload["birthYear"].is_null());
        CHECK_EQ(result.payload["birthYearUpdatedAt"].get<int64_t>(), 3000);

        json unstated = base_payload();
        result = merge_sync_payloads(unstated, local);
        CHECK_EQ(result.payload["birthYear"].get<int64_t>(), 1990);

        CHECK(fingerprint_state(normalize_sync_state(local)) !=
              fingerprint_state(normalize_sync_state(remote)));
    }

    return test_result();
}
