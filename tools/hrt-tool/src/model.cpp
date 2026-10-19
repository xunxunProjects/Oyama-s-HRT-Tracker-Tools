#include "model.h"

#include "error.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool is_protected_id(const std::string &id) {
    return id == "__proto__" || id == "constructor" || id == "prototype";
}

int64_t as_timestamp(const nlohmann::json &value) {
    double number = 0.0;
    if (value.is_number()) {
        number = value.get<double>();
    } else if (value.is_string()) {
        const std::string text = value.get<std::string>();
        if (text.empty()) {
            return 0;
        }
        char *end = nullptr;
        number = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0') {
            return 0;
        }
    } else {
        return 0;
    }
    if (!std::isfinite(number) || number <= 0.0) {
        return 0;
    }
    return static_cast<int64_t>(number);
}

std::optional<std::string> record_id(const nlohmann::json &record) {
    if (!record.is_object()) {
        return std::nullopt;
    }
    const auto found = record.find("id");
    if (found == record.end() || !found->is_string()) {
        return std::nullopt;
    }
    const std::string id = found->get<std::string>();
    if (id.empty() || is_protected_id(id)) {
        return std::nullopt;
    }
    return id;
}

int64_t record_stamp(const nlohmann::json &record) {
    if (!record.is_object() || !record.contains("updatedAt")) {
        return 0;
    }
    return as_timestamp(record.at("updatedAt"));
}

std::string canonical_number(double value) {
    if (std::isfinite(value) && value == std::trunc(value) &&
        std::fabs(value) < 9.0e15) {
        return std::to_string(static_cast<int64_t>(value));
    }
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

std::string normalize_string(const std::string &text) {
    const std::string::size_type begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return text;
    }
    const std::string::size_type end = text.find_last_not_of(" \t\r\n");
    const std::string trimmed = text.substr(begin, end - begin + 1);
    char *parse_end = nullptr;
    const double number = std::strtod(trimmed.c_str(), &parse_end);
    if (parse_end != trimmed.c_str() && *parse_end == '\0' && std::isfinite(number)) {
        return canonical_number(number);
    }
    return text;
}

std::string content_fingerprint(RecordKind kind, const nlohmann::json &record) {
    static const char *const kEventFields[] = {"route", "ester", "doseMG", "timeH",
                                               "extras"};
    static const char *const kLabFields[] = {"unit", "concValue", "timeH", "assay"};
    static const char *const kTemplateFields[] = {"name", "route", "ester", "doseMG",
                                                  "extras"};
    const char *const *fields = kEventFields;
    size_t count = sizeof(kEventFields) / sizeof(kEventFields[0]);
    if (kind == RecordKind::LabResults) {
        fields = kLabFields;
        count = sizeof(kLabFields) / sizeof(kLabFields[0]);
    } else if (kind == RecordKind::DoseTemplates) {
        fields = kTemplateFields;
        count = sizeof(kTemplateFields) / sizeof(kTemplateFields[0]);
    }

    std::string result;
    for (size_t i = 0; i < count; ++i) {
        if (i != 0) {
            result.push_back('|');
        }
        if (record.is_object() && record.contains(fields[i])) {
            result += stable_string(record.at(fields[i]));
        }
    }
    return result;
}

std::map<std::string, int64_t> sanitize_tombstone_map(const nlohmann::json &raw) {
    std::map<std::string, int64_t> out;
    if (!raw.is_object()) {
        return out;
    }
    for (const auto &item : raw.items()) {
        if (item.key().empty() || is_protected_id(item.key())) {
            continue;
        }
        const int64_t timestamp = as_timestamp(item.value());
        if (timestamp > 0) {
            out[item.key()] = timestamp;
        }
    }
    return out;
}

void route_flat_payload(const nlohmann::json &payload, SyncState &state) {
    const auto is_t_ester = [](const nlohmann::json &record) {
        if (!record.is_object() || !record.contains("ester") ||
            !record.at("ester").is_string()) {
            return false;
        }
        const std::string ester = record.at("ester").get<std::string>();
        return ester == "T" || ester == "TC" || ester == "TE" || ester == "TU";
    };
    const auto is_t_unit = [](const nlohmann::json &record) {
        if (!record.is_object() || !record.contains("unit") ||
            !record.at("unit").is_string()) {
            return false;
        }
        const std::string unit = record.at("unit").get<std::string>();
        return unit == "ng/dl" || unit == "nmol/l";
    };

    const auto route_records = [&](const char *key, RecordKind kind,
                                   bool (*classify)(const nlohmann::json &)) {
        if (!payload.contains(key) || !payload.at(key).is_array()) {
            return;
        }
        for (const nlohmann::json &record : payload.at(key)) {
            if (!record.is_object()) {
                continue;
            }
            const ModeKey mode = classify(record) ? ModeKey::Transmasc : ModeKey::Transfem;
            state.mode(mode).for_kind(kind).push_back(record);
        }
    };

    route_records("events", RecordKind::Events, is_t_ester);
    route_records("labResults", RecordKind::LabResults, is_t_unit);
    route_records("doseTemplates", RecordKind::DoseTemplates, is_t_ester);
}

ModeBlock normalize_mode_block(const nlohmann::json &raw) {
    ModeBlock block;
    if (!raw.is_object()) {
        return block;
    }
    const auto read_array = [&](const char *key, std::vector<nlohmann::json> &out) {
        if (!raw.contains(key) || !raw.at(key).is_array()) {
            return;
        }
        for (const nlohmann::json &record : raw.at(key)) {
            if (record.is_object()) {
                out.push_back(record);
            }
        }
    };
    read_array("events", block.events);
    read_array("labResults", block.lab_results);
    read_array("doseTemplates", block.dose_templates);

    if (raw.contains("deletions") && raw.at("deletions").is_object()) {
        const nlohmann::json &deletions = raw.at("deletions");
        block.deletions.events =
            sanitize_tombstone_map(deletions.value("events", nlohmann::json::object()));
        block.deletions.lab_results = sanitize_tombstone_map(
            deletions.value("labResults", nlohmann::json::object()));
        block.deletions.dose_templates = sanitize_tombstone_map(
            deletions.value("doseTemplates", nlohmann::json::object()));
    }
    return block;
}

std::map<std::string, int64_t> merge_tombstone_maps(
    const std::map<std::string, int64_t> &a,
    const std::map<std::string, int64_t> &b) {
    std::map<std::string, int64_t> out = a;
    for (const auto &[id, at] : b) {
        const auto found = out.find(id);
        if (found == out.end() || at > found->second) {
            out[id] = at;
        }
    }
    return out;
}

bool has_duplicate_ids(const std::vector<nlohmann::json> &records) {
    std::set<std::string> seen;
    for (const nlohmann::json &record : records) {
        const auto id = record_id(record);
        if (id.has_value() && !seen.insert(*id).second) {
            return true;
        }
    }
    return false;
}

std::vector<nlohmann::json> merge_kind(RecordKind kind,
                                       const std::vector<nlohmann::json> &local,
                                       const std::vector<nlohmann::json> &remote,
                                       const std::map<std::string, int64_t> &tombstones,
                                       MergeStats &stats,
                                       std::vector<std::string> &warnings,
                                       const std::string &where) {
    std::map<std::string, nlohmann::json> out;
    std::set<std::string> dropped;

    for (const nlohmann::json &record : local) {
        const auto id = record_id(record);
        if (!id.has_value()) {
            continue;
        }
        if (tombstones.count(*id) != 0) {
            if (dropped.insert(*id).second) {
                ++stats.removed;
            }
            continue;
        }
        out[*id] = record;
    }

    for (const nlohmann::json &record : remote) {
        const auto id = record_id(record);
        if (!id.has_value() || tombstones.count(*id) != 0) {
            continue;
        }
        auto mine = out.find(*id);
        if (mine == out.end()) {
            out[*id] = record;
            ++stats.added;
            continue;
        }

        const std::string mine_fp = content_fingerprint(kind, mine->second);
        const std::string theirs_fp = content_fingerprint(kind, record);
        if (mine_fp == theirs_fp) {
            const int64_t stamp =
                std::max(record_stamp(mine->second), record_stamp(record));
            if (stamp > 0 && record_stamp(mine->second) != stamp) {
                nlohmann::json updated = mine->second;
                updated["updatedAt"] = stamp;
                mine->second = updated;
            }
            continue;
        }

        const int64_t mine_at = record_stamp(mine->second);
        const int64_t theirs_at = record_stamp(record);
        const bool theirs_wins =
            theirs_at != mine_at ? theirs_at > mine_at : theirs_fp > mine_fp;
        if (theirs_at == mine_at) {
            warnings.push_back("conflict " + where + " " + *id +
                               ": resolved by content fingerprint");
        }
        if (theirs_wins) {
            mine->second = record;
            ++stats.updated;
        }
    }

    std::vector<nlohmann::json> result;
    result.reserve(out.size());
    for (const auto &[id, record] : out) {
        result.push_back(record);
    }
    return result;
}

struct ScalarResult {
    std::optional<nlohmann::json> value;
    int64_t at = 0;
};

ScalarResult resolve_scalar(const std::optional<nlohmann::json> &local, int64_t local_at,
                            const std::optional<nlohmann::json> &remote,
                            int64_t remote_at) {
    if (!local.has_value()) {
        return ScalarResult{remote, remote_at};
    }
    if (!remote.has_value()) {
        return ScalarResult{local, local_at};
    }
    if (remote_at != local_at) {
        return remote_at > local_at ? ScalarResult{remote, remote_at}
                                    : ScalarResult{local, local_at};
    }
    return stable_string(*remote) > stable_string(*local)
               ? ScalarResult{remote, remote_at}
               : ScalarResult{local, local_at};
}

std::string join_strings(const std::vector<std::string> &values, char separator) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out.push_back(separator);
        }
        out += values[i];
    }
    return out;
}

} // namespace

const char *mode_key_name(ModeKey mode) {
    return mode == ModeKey::Transmasc ? "transmasc" : "transfem";
}

const char *record_kind_name(RecordKind kind) {
    switch (kind) {
    case RecordKind::Events:
        return "events";
    case RecordKind::LabResults:
        return "labResults";
    case RecordKind::DoseTemplates:
        return "doseTemplates";
    }
    return "events";
}

std::map<std::string, int64_t> &Tombstones::for_kind(RecordKind kind) {
    switch (kind) {
    case RecordKind::Events:
        return events;
    case RecordKind::LabResults:
        return lab_results;
    case RecordKind::DoseTemplates:
        return dose_templates;
    }
    return events;
}

const std::map<std::string, int64_t> &Tombstones::for_kind(RecordKind kind) const {
    switch (kind) {
    case RecordKind::Events:
        return events;
    case RecordKind::LabResults:
        return lab_results;
    case RecordKind::DoseTemplates:
        return dose_templates;
    }
    return events;
}

std::vector<nlohmann::json> &ModeBlock::for_kind(RecordKind kind) {
    switch (kind) {
    case RecordKind::Events:
        return events;
    case RecordKind::LabResults:
        return lab_results;
    case RecordKind::DoseTemplates:
        return dose_templates;
    }
    return events;
}

const std::vector<nlohmann::json> &ModeBlock::for_kind(RecordKind kind) const {
    switch (kind) {
    case RecordKind::Events:
        return events;
    case RecordKind::LabResults:
        return lab_results;
    case RecordKind::DoseTemplates:
        return dose_templates;
    }
    return events;
}

ModeBlock &SyncState::mode(ModeKey key) {
    return modes[static_cast<size_t>(key)];
}

const ModeBlock &SyncState::mode(ModeKey key) const {
    return modes[static_cast<size_t>(key)];
}

std::string stable_string(const nlohmann::json &value) {
    if (value.is_null()) {
        return "";
    }
    if (value.is_array()) {
        std::vector<std::string> parts;
        parts.reserve(value.size());
        for (const nlohmann::json &item : value) {
            parts.push_back(stable_string(item));
        }
        return "[" + join_strings(parts, ',') + "]";
    }
    if (value.is_object()) {
        std::vector<std::string> parts;
        parts.reserve(value.size());
        for (const auto &item : value.items()) {
            parts.push_back(item.key() + ":" + stable_string(item.value()));
        }
        return "{" + join_strings(parts, ',') + "}";
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<int64_t>());
    }
    if (value.is_number_unsigned()) {
        return std::to_string(value.get<uint64_t>());
    }
    if (value.is_number_float()) {
        return canonical_number(value.get<double>());
    }
    if (value.is_string()) {
        return normalize_string(value.get<std::string>());
    }
    return value.dump();
}

SyncState normalize_sync_state(const nlohmann::json &payload) {
    SyncState state;
    if (payload.is_null()) {
        return state;
    }
    if (payload.is_array()) {
        route_flat_payload(nlohmann::json{{"events", payload}}, state);
        return state;
    }
    if (!payload.is_object()) {
        return state;
    }

    if (payload.contains("modes") && payload.at("modes").is_object()) {
        const nlohmann::json &modes = payload.at("modes");
        for (ModeKey mode : {ModeKey::Transfem, ModeKey::Transmasc}) {
            const char *name = mode_key_name(mode);
            if (modes.contains(name) && modes.at(name).is_object()) {
                state.mode(mode) = normalize_mode_block(modes.at(name));
            }
        }
    } else {
        route_flat_payload(payload, state);
    }

    if (payload.contains("weight") && payload.at("weight").is_number()) {
        const double weight = payload.at("weight").get<double>();
        if (std::isfinite(weight) && weight > 0.0) {
            state.weight = weight;
            state.weight_updated_at =
                as_timestamp(payload.value("weightUpdatedAt", nlohmann::json()));
            if (state.weight_updated_at == 0) {
                state.weight_updated_at = 1;
            }
        }
    }
    if (payload.contains("pkParams")) {
        state.pk_params = payload.at("pkParams");
        state.pk_params_updated_at =
            as_timestamp(payload.value("pkParamsUpdatedAt", nlohmann::json()));
        if (state.pk_params_updated_at == 0) {
            state.pk_params_updated_at = 1;
        }
    }
    return state;
}

nlohmann::json to_sync_payload(const SyncState &state, const nlohmann::json &base) {
    nlohmann::json out = base.is_object() ? base : nlohmann::json::object();
    if (!out.contains("meta") || !out.at("meta").is_object()) {
        out["meta"] = nlohmann::json::object();
    }
    out["meta"]["version"] = 2;
    const ModeKey active = declared_mode(out).value_or(ModeKey::Transfem);
    out["mode"] = mode_key_name(active);

    nlohmann::json modes = nlohmann::json::object();
    for (ModeKey mode : {ModeKey::Transfem, ModeKey::Transmasc}) {
        const ModeBlock &block = state.mode(mode);
        nlohmann::json out_block = nlohmann::json::object();
        nlohmann::json deletions = nlohmann::json::object();
        for (RecordKind kind : {RecordKind::Events, RecordKind::LabResults,
                                RecordKind::DoseTemplates}) {
            const char *key = record_kind_name(kind);
            nlohmann::json records = nlohmann::json::array();
            for (const nlohmann::json &record : block.for_kind(kind)) {
                records.push_back(record);
            }
            out_block[key] = records;

            nlohmann::json map = nlohmann::json::object();
            for (const auto &[id, at] : block.deletions.for_kind(kind)) {
                map[id] = at;
            }
            deletions[key] = map;
        }
        out_block["deletions"] = deletions;
        modes[mode_key_name(mode)] = out_block;
    }
    out["modes"] = modes;

    if (state.weight.has_value()) {
        out["weight"] = *state.weight;
    } else {
        out.erase("weight");
    }
    if (state.weight_updated_at > 0) {
        out["weightUpdatedAt"] = state.weight_updated_at;
    } else {
        out.erase("weightUpdatedAt");
    }
    if (state.pk_params.has_value()) {
        out["pkParams"] = *state.pk_params;
    } else {
        out.erase("pkParams");
    }
    if (state.pk_params_updated_at > 0) {
        out["pkParamsUpdatedAt"] = state.pk_params_updated_at;
    } else {
        out.erase("pkParamsUpdatedAt");
    }
    return out;
}

nlohmann::json normalize_sync_payload(const nlohmann::json &payload) {
    return to_sync_payload(normalize_sync_state(payload), payload);
}

MergeResult merge_sync_payloads(const nlohmann::json &local,
                                const nlohmann::json &remote) {
    MergeResult result;
    const SyncState local_state = normalize_sync_state(local);
    const SyncState remote_state = normalize_sync_state(remote);
    SyncState merged;

    for (ModeKey mode : {ModeKey::Transfem, ModeKey::Transmasc}) {
        for (RecordKind kind : {RecordKind::Events, RecordKind::LabResults,
                                RecordKind::DoseTemplates}) {
            const std::string where =
                std::string(mode_key_name(mode)) + "." + record_kind_name(kind);
            const std::vector<nlohmann::json> &local_records =
                local_state.mode(mode).for_kind(kind);
            const std::vector<nlohmann::json> &remote_records =
                remote_state.mode(mode).for_kind(kind);

            merged.mode(mode).deletions.for_kind(kind) =
                merge_tombstone_maps(local_state.mode(mode).deletions.for_kind(kind),
                                     remote_state.mode(mode).deletions.for_kind(kind));

            if (has_duplicate_ids(local_records) || has_duplicate_ids(remote_records)) {
                result.aborted = true;
                result.warnings.push_back("duplicate id in " + where +
                                          "; category left unchanged");
                merged.mode(mode).for_kind(kind) = local_records;
                continue;
            }

            merged.mode(mode).for_kind(kind) =
                merge_kind(kind, local_records, remote_records,
                           merged.mode(mode).deletions.for_kind(kind), result.stats,
                           result.warnings, where);
        }
    }

    const std::optional<nlohmann::json> local_weight =
        local_state.weight.has_value()
            ? std::optional<nlohmann::json>(nlohmann::json(*local_state.weight))
            : std::nullopt;
    const std::optional<nlohmann::json> remote_weight =
        remote_state.weight.has_value()
            ? std::optional<nlohmann::json>(nlohmann::json(*remote_state.weight))
            : std::nullopt;
    const ScalarResult weight =
        resolve_scalar(local_weight, local_state.weight_updated_at, remote_weight,
                       remote_state.weight_updated_at);
    if (weight.value.has_value() && weight.value->is_number()) {
        merged.weight = weight.value->get<double>();
        merged.weight_updated_at = weight.at;
    }

    const ScalarResult pk =
        resolve_scalar(local_state.pk_params, local_state.pk_params_updated_at,
                       remote_state.pk_params, remote_state.pk_params_updated_at);
    merged.pk_params = pk.value;
    merged.pk_params_updated_at = pk.at;

    result.payload = to_sync_payload(merged, local);
    result.local_changed =
        fingerprint_state(merged) != fingerprint_state(local_state);
    result.remote_stale =
        fingerprint_state(merged) != fingerprint_state(remote_state);
    return result;
}

nlohmann::json &mode_block(nlohmann::json &payload, ModeKey mode) {
    if (!payload.is_object()) {
        throw ToolError(ExitCode::Data, "MODEL", "sync payload is not an object");
    }
    if (!payload.contains("modes") || !payload.at("modes").is_object()) {
        payload["modes"] = nlohmann::json::object();
    }
    nlohmann::json &modes = payload["modes"];
    const char *name = mode_key_name(mode);
    if (!modes.contains(name) || !modes.at(name).is_object()) {
        modes[name] = nlohmann::json::object();
    }
    return modes[name];
}

void write_tombstone(nlohmann::json &payload, ModeKey mode, RecordKind kind,
                     const std::string &id, int64_t now_ms) {
    if (id.empty() || is_protected_id(id)) {
        throw ToolError(ExitCode::Data, "MODEL_ID", "invalid record id");
    }
    nlohmann::json &block = mode_block(payload, mode);
    if (!block.contains("deletions") || !block.at("deletions").is_object()) {
        block["deletions"] = nlohmann::json::object();
    }
    nlohmann::json &deletions = block["deletions"];
    const char *key = record_kind_name(kind);
    if (!deletions.contains(key) || !deletions.at(key).is_object()) {
        deletions[key] = nlohmann::json::object();
    }
    deletions[key][id] = now_ms;

    if (block.contains(key) && block.at(key).is_array()) {
        nlohmann::json &records = block[key];
        records.erase(std::remove_if(records.begin(), records.end(),
                                     [&](const nlohmann::json &record) {
                                         const auto record_value = record_id(record);
                                         return record_value.has_value() &&
                                                *record_value == id;
                                     }),
                      records.end());
    }

    if (!payload.contains("meta") || !payload.at("meta").is_object()) {
        payload["meta"] = nlohmann::json::object();
    }
    payload["meta"]["version"] = 2;
}

void clear_tombstone(nlohmann::json &payload, ModeKey mode, RecordKind kind,
                     const std::string &id) {
    if (!payload.is_object() || !payload.contains("modes") ||
        !payload.at("modes").is_object()) {
        return;
    }
    nlohmann::json &modes = payload["modes"];
    const char *name = mode_key_name(mode);
    if (!modes.contains(name) || !modes.at(name).is_object()) {
        return;
    }
    nlohmann::json &block = modes[name];
    if (!block.contains("deletions") || !block.at("deletions").is_object()) {
        return;
    }
    nlohmann::json &deletions = block["deletions"];
    const char *key = record_kind_name(kind);
    if (deletions.contains(key) && deletions.at(key).is_object()) {
        deletions[key].erase(id);
    }
}

void prune_tombstones(nlohmann::json &payload, int64_t now_ms) {
    if (!payload.is_object() || !payload.contains("modes") ||
        !payload.at("modes").is_object()) {
        return;
    }
    nlohmann::json &modes = payload["modes"];
    for (ModeKey mode : {ModeKey::Transfem, ModeKey::Transmasc}) {
        const char *name = mode_key_name(mode);
        if (!modes.contains(name) || !modes.at(name).is_object()) {
            continue;
        }
        nlohmann::json &block = modes[name];
        if (!block.contains("deletions") || !block.at("deletions").is_object()) {
            continue;
        }
        nlohmann::json &deletions = block["deletions"];
        for (RecordKind kind : {RecordKind::Events, RecordKind::LabResults,
                                RecordKind::DoseTemplates}) {
            const char *key = record_kind_name(kind);
            if (!deletions.contains(key) || !deletions.at(key).is_object()) {
                continue;
            }
            std::vector<std::pair<std::string, int64_t>> entries;
            for (const auto &item : deletions.at(key).items()) {
                const int64_t timestamp = as_timestamp(item.value());
                if (timestamp > 0 && now_ms - timestamp < kTombstoneTtlMs) {
                    entries.emplace_back(item.key(), timestamp);
                }
            }
            std::sort(entries.begin(), entries.end(),
                      [](const auto &a, const auto &b) {
                          if (a.second != b.second) {
                              return a.second > b.second;
                          }
                          return a.first < b.first;
                      });
            if (entries.size() > kTombstoneMaxPerKind) {
                entries.resize(kTombstoneMaxPerKind);
            }
            nlohmann::json pruned = nlohmann::json::object();
            for (const auto &[id, timestamp] : entries) {
                pruned[id] = timestamp;
            }
            deletions[key] = pruned;
        }
    }
}

std::optional<ModeKey> declared_mode(const nlohmann::json &payload) {
    if (!payload.is_object() || !payload.contains("mode") ||
        !payload.at("mode").is_string()) {
        return std::nullopt;
    }
    const std::string mode = payload.at("mode").get<std::string>();
    if (mode == "transfem") {
        return ModeKey::Transfem;
    }
    if (mode == "transmasc") {
        return ModeKey::Transmasc;
    }
    return std::nullopt;
}

ModeKey active_mode(const nlohmann::json &payload) {
    return declared_mode(payload).value_or(ModeKey::Transfem);
}

std::string fingerprint_state(const SyncState &state) {
    std::vector<std::string> parts;
    for (ModeKey mode : {ModeKey::Transfem, ModeKey::Transmasc}) {
        const ModeBlock &block = state.mode(mode);
        for (RecordKind kind : {RecordKind::Events, RecordKind::LabResults,
                                RecordKind::DoseTemplates}) {
            std::vector<std::string> rows;
            for (const nlohmann::json &record : block.for_kind(kind)) {
                const auto id = record_id(record);
                if (id.has_value()) {
                    rows.push_back(*id + "=" + content_fingerprint(kind, record));
                }
            }
            std::sort(rows.begin(), rows.end());
            parts.push_back(std::string(mode_key_name(mode)) + "." +
                            record_kind_name(kind) + ":" +
                            join_strings(rows, ';'));

            std::vector<std::string> keys;
            for (const auto &[id, at] : block.deletions.for_kind(kind)) {
                keys.push_back(id);
            }
            std::sort(keys.begin(), keys.end());
            parts.push_back(std::string(mode_key_name(mode)) + "." +
                            record_kind_name(kind) + ".del:" +
                            join_strings(keys, ';'));
        }
    }
    parts.push_back("weight:" + (state.weight.has_value()
                                     ? stable_string(nlohmann::json(*state.weight))
                                     : std::string{}));
    parts.push_back("pkParams:" +
                    (state.pk_params.has_value() ? stable_string(*state.pk_params)
                                                 : std::string{}));
    return join_strings(parts, '\n');
}
