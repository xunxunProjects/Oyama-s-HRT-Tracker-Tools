#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

enum class ModeKey { Transfem = 0, Transmasc = 1 };
enum class RecordKind { Events = 0, LabResults = 1, DoseTemplates = 2 };

constexpr int64_t kTombstoneTtlMs = 180LL * 24 * 60 * 60 * 1000;
constexpr size_t kTombstoneMaxPerKind = 5000;

const char *mode_key_name(ModeKey mode);
const char *record_kind_name(RecordKind kind);

struct Tombstones {
    std::map<std::string, int64_t> events;
    std::map<std::string, int64_t> lab_results;
    std::map<std::string, int64_t> dose_templates;

    std::map<std::string, int64_t> &for_kind(RecordKind kind);
    const std::map<std::string, int64_t> &for_kind(RecordKind kind) const;
};

struct ModeBlock {
    std::vector<nlohmann::json> events;
    std::vector<nlohmann::json> lab_results;
    std::vector<nlohmann::json> dose_templates;
    Tombstones deletions;

    std::vector<nlohmann::json> &for_kind(RecordKind kind);
    const std::vector<nlohmann::json> &for_kind(RecordKind kind) const;
};

struct SyncState {
    std::array<ModeBlock, 2> modes;
    std::optional<double> weight;
    int64_t weight_updated_at = 0;
    /* nullopt = the payload said nothing; a JSON null = explicitly cleared. */
    std::optional<nlohmann::json> pk_params;
    int64_t pk_params_updated_at = 0;

    ModeBlock &mode(ModeKey key);
    const ModeBlock &mode(ModeKey key) const;
};

struct MergeStats {
    int added = 0;
    int updated = 0;
    int removed = 0;
};

struct MergeResult {
    nlohmann::json payload;
    MergeStats stats;
    std::vector<std::string> warnings;
    bool aborted = false;
    bool local_changed = false;
    bool remote_stale = false;
};

struct ImportSanitizeReport {
    int dropped = 0;
    int coerced = 0;
};

/*
 * Applies the website's import sanitizers (backupState.ts): unknown routes
 * and out-of-range event times are dropped, doses are clamped to
 * [0, 10000] mg, invalid esters default to E2, invalid lab units default to
 * pmol/l, weights are clamped to [20, 400] kg, and missing ids get one.
 */
nlohmann::json sanitize_import_payload(const nlohmann::json &payload,
                                       ImportSanitizeReport *report);

struct MergeOptions {
    /*
     * An explicit `import` is the user asking for these records back, so a
     * tombstone on the local side does not block an imported record. Cloud
     * sync (the default) keeps tombstone-wins.
     */
    bool explicit_import = false;
};

SyncState normalize_sync_state(const nlohmann::json &payload);
nlohmann::json to_sync_payload(const SyncState &state, const nlohmann::json &base);
nlohmann::json normalize_sync_payload(const nlohmann::json &payload);

MergeResult merge_sync_payloads(const nlohmann::json &local,
                                const nlohmann::json &remote,
                                const MergeOptions &options = MergeOptions{});

nlohmann::json &mode_block(nlohmann::json &payload, ModeKey mode);
void write_tombstone(nlohmann::json &payload, ModeKey mode, RecordKind kind,
                     const std::string &id, int64_t now_ms);
void clear_tombstone(nlohmann::json &payload, ModeKey mode, RecordKind kind,
                     const std::string &id);
void prune_tombstones(nlohmann::json &payload, int64_t now_ms);

std::optional<ModeKey> declared_mode(const nlohmann::json &payload);
ModeKey active_mode(const nlohmann::json &payload);

std::string stable_string(const nlohmann::json &value);
std::string fingerprint_state(const SyncState &state);
