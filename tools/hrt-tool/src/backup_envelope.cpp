#include "backup_envelope.h"

#include "error.h"
#include "model.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

void add_not_carried(std::vector<std::string> &items, const std::string &name) {
    if (std::find(items.begin(), items.end(), name) == items.end()) {
        items.push_back(name);
    }
}

nlohmann::json records_json(const std::vector<nlohmann::json> &records) {
    nlohmann::json out = nlohmann::json::array();
    for (const nlohmann::json &record : records) {
        out.push_back(record);
    }
    return out;
}

nlohmann::json tombstones_json(const std::map<std::string, int64_t> &tombstones) {
    nlohmann::json out = nlohmann::json::object();
    for (const auto &[id, at] : tombstones) {
        out[id] = at;
    }
    return out;
}

} // namespace

nlohmann::json export_complete_envelope(const nlohmann::json &sync_payload,
                                        const std::string &created_at) {
    const SyncState state = normalize_sync_state(sync_payload);
    nlohmann::json envelope;
    envelope["format"] = "oyama-hrt-complete-backup";
    envelope["formatVersion"] = 1;
    envelope["createdAt"] = created_at;
    envelope["mode"] =
        mode_key_name(declared_mode(sync_payload).value_or(ModeKey::Transfem));

    nlohmann::json modes = nlohmann::json::object();
    for (ModeKey mode : {ModeKey::Transfem, ModeKey::Transmasc}) {
        const ModeBlock &block = state.mode(mode);
        nlohmann::json out = nlohmann::json::object();
        out["events"] = records_json(block.events);
        out["labResults"] = records_json(block.lab_results);
        out["doseTemplates"] = records_json(block.dose_templates);
        out["deletions"] = nlohmann::json{
            {"events", tombstones_json(block.deletions.events)},
            {"labResults", tombstones_json(block.deletions.lab_results)},
            {"doseTemplates", tombstones_json(block.deletions.dose_templates)},
        };
        modes[mode_key_name(mode)] = out;
    }
    envelope["modes"] = modes;

    if (state.weight.has_value()) {
        envelope["weight"] = *state.weight;
    }
    if (state.weight_updated_at > 0) {
        envelope["weightUpdatedAt"] = state.weight_updated_at;
    }
    if (state.pk_params.has_value()) {
        envelope["pkParams"] = *state.pk_params;
    }
    if (state.pk_params_updated_at > 0) {
        envelope["pkParamsUpdatedAt"] = state.pk_params_updated_at;
    }
    return envelope;
}

EnvelopeImport import_complete_envelope(const nlohmann::json &envelope) {
    if (!envelope.is_object()) {
        throw ToolError(ExitCode::Data, "ENVELOPE_FORMAT",
                        "complete backup is not a JSON object");
    }
    if (!envelope.contains("format") || !envelope.at("format").is_string() ||
        envelope.at("format").get<std::string>() != "oyama-hrt-complete-backup") {
        throw ToolError(ExitCode::Data, "ENVELOPE_FORMAT",
                        "not an oyama-hrt-complete-backup file");
    }
    if (!envelope.contains("formatVersion") ||
        !envelope.at("formatVersion").is_number() ||
        envelope.at("formatVersion").get<int>() != 1) {
        throw ToolError(ExitCode::Data, "ENVELOPE_VERSION",
                        "unsupported complete backup version");
    }
    if (!envelope.contains("mode") || !envelope.at("mode").is_string()) {
        throw ToolError(ExitCode::Data, "ENVELOPE_MODE", "complete backup has no mode");
    }
    const std::string mode_name = envelope.at("mode").get<std::string>();
    if (mode_name != "transfem" && mode_name != "transmasc") {
        throw ToolError(ExitCode::Data, "ENVELOPE_MODE",
                        "complete backup mode must be transfem or transmasc");
    }
    if (!envelope.contains("modes") || !envelope.at("modes").is_object()) {
        throw ToolError(ExitCode::Data, "ENVELOPE_MODES",
                        "complete backup has no modes object");
    }

    nlohmann::json sync;
    sync["meta"] = nlohmann::json{
        {"version", 2},
        {"exportedAt", envelope.value("createdAt", std::string{})},
    };
    sync["mode"] = mode_name;
    sync["modes"] = nlohmann::json::object();

    EnvelopeImport result;
    for (ModeKey mode : {ModeKey::Transfem, ModeKey::Transmasc}) {
        const char *name = mode_key_name(mode);
        const nlohmann::json &source = envelope.at("modes");
        nlohmann::json block = nlohmann::json::object();
        if (source.contains(name) && source.at(name).is_object()) {
            const nlohmann::json &raw = source.at(name);
            block["events"] = raw.value("events", nlohmann::json::array());
            block["labResults"] = raw.value("labResults", nlohmann::json::array());
            block["doseTemplates"] = raw.value("doseTemplates", nlohmann::json::array());
            block["deletions"] = raw.value("deletions", nlohmann::json::object());
            for (const char *extra : {"quickDoses", "supplies", "plan"}) {
                if (raw.contains(extra)) {
                    add_not_carried(result.not_carried, extra);
                }
            }
        } else {
            block["events"] = nlohmann::json::array();
            block["labResults"] = nlohmann::json::array();
            block["doseTemplates"] = nlohmann::json::array();
            block["deletions"] = nlohmann::json::object();
        }
        sync["modes"][name] = block;
    }

    if (envelope.contains("weight")) {
        sync["weight"] = envelope.at("weight");
    }
    if (envelope.contains("weightUpdatedAt")) {
        sync["weightUpdatedAt"] = envelope.at("weightUpdatedAt");
    }
    if (envelope.contains("pkParams")) {
        sync["pkParams"] = envelope.at("pkParams");
    }
    if (envelope.contains("pkParamsUpdatedAt")) {
        sync["pkParamsUpdatedAt"] = envelope.at("pkParamsUpdatedAt");
    }

    if (envelope.contains("preferences")) {
        add_not_carried(result.not_carried, "preferences");
    }
    if (envelope.contains("calibration")) {
        add_not_carried(result.not_carried, "calibration");
    }

    result.sync_payload = normalize_sync_payload(sync);
    return result;
}
