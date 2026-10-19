# hrt-tool M2 Local Data Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the local pharmacokinetic engine, cloud-snapshot decryption, the sync-payload merge rules, encrypted export/import, the Rust printer, and the `look`/`add`/`dose`/`lab`/`weight`/`export`/`import` commands. The server stays stubbed; M2 works against a local `--snapshot` file.

**Architecture:** The C core (`hrtcore`) owns the PK math and exposes a bounded C ABI. The C++ layer parses JSON and snapshots, applies the sync merge/tombstone rules, and calls the Rust printer for human/JSON output. The vault master password protects local exports; the account password + user id derive the cloud snapshot key. No plaintext reaches a regular file.

**Tech Stack:** C11, C++17, CMake, OpenSSL, libcurl, nlohmann/json, Rust (serde_json), CTest.

**Spec:** `docs/superpowers/specs/2026-10-19-hrt-tool-design.md`

## Global Constraints

- TDD: every behavior gets a test that is watched failing first.
- hrt-cold, root CMake, `include/hrtcold/`, and `tests/native/` stay untouched.
- Limits: 16 MiB parse, 100,000 events, 10,000 labs, 1,000 query points, 100,000 PK grid steps.
- No plaintext in a regular file: snapshots, exports, vault, and logs are encrypted; plaintext only to stdout.
- Deletes write tombstones; imports merge by default; `--replace` requires explicit confirmation.
- The PK engine must match `logic.ts`; the reference vectors in `tools/hrt-tool/testdata/pk-vectors.json` are generated from it.
- Calibration (EKF/OU/MIPD) is not ported in M2; `look` says the curve is uncalibrated.
- Every commit ends with `Co-Authored-By: Claude Code <noreply@anthropic.com>`.

## Review Focus

1. PK parity: sampled concentrations, grid length, AUC, and patch pairing match the TS vectors.
2. Snapshot auth: wrong password, tampered ciphertext, or a changed IV fails closed with no output.
3. Merge safety: concurrent imports cannot drop a record, duplicate an id, or resurrect a tombstoned one.
4. Plaintext at rest: no snapshot/export/temp file contains the payload or a secret.
5. Malformed or oversized input fails with a typed error, never a crash or OOM.

---

### Task 1: PK ABI, C++ harness, and extended vectors

**Files:**
- Modify: `tools/hrt-tool/include/hrtcore/hrtcore.h`
- Create: `tools/hrt-tool/include/hrtcore/pk.h` (or a clearly separated section in `hrtcore.h`)
- Create: `tools/hrt-tool/src/pk.h`
- Create: `tools/hrt-tool/src/pk.cpp`
- Create: `tools/hrt-tool/tests/pk_test.cpp`
- Modify: `tools/hrt-tool/testdata/pk-vectors.json`
- Modify: `tools/hrt-tool/CMakeLists.txt`

**Interfaces:**
- C: `hrtcore_route`, `hrtcore_ester`, `hrtcore_extra`, `hrtcore_dose_event`, `hrtcore_pk_params`, `hrtcore_pk_result`, `hrtcore_pk_defaults`, `hrtcore_pk_run`, `hrtcore_pk_result_free`, `hrtcore_pk_interp_e2/cpa/t`.
- C++: `PkEvent`, `PkParams`, `PkSample`, `PkResult`, `default_pk_params()`, `sanitize_pk_params(json)`, `run_pk(events, weight, params, now_h)`, `interpolate(...)`.

- [ ] **Step 1: Extend and regenerate the vectors**

Add patch scenarios to the temporary generator run from the main repo: two staggered patches with explicit removals, a planned-wear patch with no removal, and an over-wear removal. Regenerate `testdata/pk-vectors.json`. Confirm 27 scenarios and the snapshot fixture are present.

- [ ] **Step 2: Write the failing PK vector test**

`tests/pk_test.cpp` loads the JSON, builds `PkEvent`/`PkParams`, calls `run_pk`, and asserts `gridLength`, `auc`, the seven grid points, and all 19 samples against the vectors with relative tolerance `1e-7` (absolute `1e-9` for zeros). `run_pk` initially returns `std::nullopt`/throws `NOT_IMPLEMENTED`.

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build tools/hrt-tool/build && ctest -R pk --output-on-failure`
Expected: the test fails because the PK run is not implemented.

- [ ] **Step 4: Implement the C ABI declarations and the C++ wrapper skeleton**

Define the enums/structs exactly as above, `extras_present` as a bitmask, `hrtcore_pk_defaults`, and the result-free function. The C++ `sanitize_pk_params` ports `sanitizePKParams`/`PK_PARAM_RANGES`/former defaults; `run_pk` throws `NOT_IMPLEMENTED` until Task 2.

- [ ] **Step 5: Register the test and commit**

Add `native/core/pk.c` as an empty implementation that returns `HRTCORE_ERR_NOT_IMPLEMENTED`, and `pk_test` to CTest. Commit the ABI/harness.

---

### Task 2: PK C core

**Files:**
- Modify: `tools/hrt-tool/native/core/pk.c`
- Modify: `tools/hrt-tool/src/pk.cpp`
- Modify: `tools/hrt-tool/tests/pk_test.cpp`

- [ ] **Step 1: Port constants and helpers**

Port from `logic.ts`: `CorePK`, `EsterInfo` MWs, `T_CorePK`, `T_DepotPK`, `T_EsterPK`, `T_GelPK`, `T_PatchPK`, `TwoPartDepotPK`, `InjectionPK`, `EsterPK`, `OralPK`, `CPA_ORAL`, `SublingualTierParams`, `DEFAULT_PK_PARAMS`. Implement `getToE2Factor`, `getBioavailabilityMultiplier`, the per-route bioavailability getters, `_getSLTheta`, `_expDivDiff1`, `_expDivDiff2`, `_analytic2C`, `_analytic3C`, `oneCompAmount`.

- [ ] **Step 2: Port route resolution and event amounts**

Port `resolveParams` exactly, including the T/CPA special cases, sublingual dual branch, patch zero-order/first-order paths, and the `patchWearH`/paired-removal logic. Port `patchRemovalTimes` (sorting, hormone matching, grace/overwear fallbacks, deterministic tie-breaks) and `PrecomputedEventModel`.

- [ ] **Step 3: Port the simulation grid and interpolation**

Port `computeMaxLifetimeH` and `runSimulation`: start = first dose − 24, end = max(last dose + 14 d, now + 24), steps = clamp(ceil(totalHours), 2000, 100000), peri-event offsets, sorted unique times, sliding active window, E2/CPA/T arrays, AUC. Port the three binary-search linear interpolators.

- [ ] **Step 4: Run the vector tests**

Run: `ctest -R pk --output-on-failure`
Expected: all 27 scenarios pass at the chosen tolerance.

- [ ] **Step 5: Commit**

---

### Task 3: Sync payload model, merge, and tombstones

**Files:**
- Create: `tools/hrt-tool/src/model.h`, `src/model.cpp`
- Create: `tools/hrt-tool/tests/model_test.cpp`
- Modify: `tools/hrt-tool/CMakeLists.txt`

**Interfaces:**
- `enum class ModeKey { Transfem, Transmasc };`
- `ModeKey active_mode(const nlohmann::json&);`
- `nlohmann::json& mode_block(nlohmann::json&, ModeKey);`
- `nlohmann::json normalize_sync_payload(const nlohmann::json&);` (v2, v1 flat, naked array)
- `struct MergeResult { nlohmann::json payload; std::vector<std::string> warnings; bool aborted; };`
- `MergeResult merge_sync_payloads(const json& local, const json& remote);`
- `void write_tombstone(json& payload, ModeKey, RecordKind, const std::string& id, int64_t now_ms);`
- `void prune_tombstones(json& payload, int64_t now_ms);`

- [ ] **Step 1: Write failing tests**

Cover: v2 parse; v1 flat routing into the active mode; unknown fields preserved; merge new id; newer `updatedAt` wins; older skipped; equal timestamp with different content aborts that category; duplicate id aborts; tombstone union; restored record clears its tombstone; expired tombstones pruned; `quickDoses`/`supplies`/`plan` absent in the cloud payload are not invented.

- [ ] **Step 2: Run to verify failure, implement, run to pass**

Port the event/lab/template subset of `syncMerge.ts`. Use `stableString`/fingerprint semantics for the conflict check. Keep the payload as JSON so unknown fields survive.

- [ ] **Step 3: Commit**

---

### Task 4: Cloud snapshot crypto and local load/save

**Files:**
- Create: `tools/hrt-tool/src/snapshot.h`, `src/snapshot.cpp`
- Create: `tools/hrt-tool/tests/snapshot_test.cpp`
- Modify: `tools/hrt-tool/src/commands.cpp` (account password flags), `src/cli.cpp`, `src/commands.h`

**Interfaces:**
- `std::array<uint8_t,32> derive_cloud_key(const std::string& password, const std::string& user_id);`
- `std::optional<nlohmann::json> decrypt_cloud_bundle(const json& bundle, const array& key);`
- `json encrypt_cloud_bundle(const json& payload, const array& key);`
- `json load_snapshot(const path&, const std::string& account_password, const std::string& user_id);`
- `void save_snapshot(const path&, const json& payload, const std::string& account_password, const std::string& user_id);`

- [ ] **Step 1: Write failing tests**

Use the fixture's `password`/`userId`/`bundle`: decrypt and compare the payload; re-encrypt and round-trip; wrong password fails with no output; tampered `data`/`iv` fails; a plaintext legacy payload is accepted; written files are 0600 and contain no plaintext; >16 MiB input is rejected.

- [ ] **Step 2: Implement**

Derive the key with PBKDF2-SHA256 600k over `hrt-cloud-v1:<userId>`, base64-decode the bundle fields, AES-256-GCM decrypt/encrypt, parse JSON. Write atomically (temp 0600, fsync, rename, dir fsync). Add `--user-id`, `--account-password-stdin`, `--account-password-file` global flags; the password prompt uses the limited reader and 256-byte cap.

- [ ] **Step 3: Run tests and commit**

---

### Task 5: Encrypted export container and complete-backup envelope

**Files:**
- Create: `tools/hrt-tool/src/encrypted_file.h`, `src/encrypted_file.cpp`
- Create: `tools/hrt-tool/src/backup_envelope.h`, `src/backup_envelope.cpp`
- Create: `tools/hrt-tool/tests/encrypted_file_test.cpp`, `tests/backup_envelope_test.cpp`

**Interfaces:**
- `void seal_encrypted_file(path, const std::string& magic, const std::string& plaintext, const std::string& password);`
- `std::string open_encrypted_file(path, const std::string& magic, const std::string& password);`
- `json export_complete_envelope(const json& sync_payload, const std::string& createdAt);`
- `struct EnvelopeImport { json sync_payload; std::vector<std::string> not_carried; };`
- `EnvelopeImport import_complete_envelope(const json& envelope);`

- [ ] **Step 1: Write failing tests**

Round-trip an `HRTE` file; wrong password/tamper fails; mode 0600; no plaintext. Envelope export carries the mode blocks/weight/pkParams and omits unsupported categories. Envelope import reports `quickDoses`, `supplies`, `plan`, `preferences`, `calibration` as not carried. Reject a wrong format/version/mode.

- [ ] **Step 2: Implement and run**

Reuse the vault's KDF/AEAD pattern with magic `HRTE`. Validate the envelope's `format`/`formatVersion`/`modes` and preserve unknown fields.

- [ ] **Step 3: Commit**

---

### Task 6: Rust printer with serde_json

**Files:**
- Modify: `tools/hrt-tool/rust/hrt-print/Cargo.toml`, `src/lib.rs`
- Create: `tools/hrt-tool/rust/hrt-print/tests/render.rs`
- Modify: `tools/hrt-tool/include/hrtprint/hrtprint.h`
- Modify: `tools/hrt-tool/CMakeLists.txt`

**Interfaces:**
- `int hrtprint_render(const char* json, const char* format, char* out, size_t out_cap, size_t* out_len, char* err, size_t err_cap);`
- Formats: `table` (aligned human output), `json` (stable pretty JSON), `error`.

- [ ] **Step 1: Write failing Rust tests**

Feed a `look` view model; assert the table contains the headers and rows, the JSON round-trips, invalid JSON returns a nonzero code, and an oversized input (16 MiB cap) fails closed. The crate may use `serde_json` (offline cache present); pin it in `Cargo.lock`.

- [ ] **Step 2: Implement and wire**

The C++ layer builds a compact view model (`now`, `mode`, `weightKg`, `compounds`, `labs`, `warnings`) and calls `hrtprint_render`; no full grid crosses the ABI. CMake builds with `cargo --release --offline`.

- [ ] **Step 3: Commit**

---

### Task 7: Local data commands

**Files:**
- Modify: `tools/hrt-tool/src/commands.cpp`, `src/commands.h`, `src/cli.cpp`, `src/model.cpp`, `src/snapshot.cpp`, `src/pk.cpp`
- Create: `tools/hrt-tool/tests/commands_local_test.cpp`

**Commands:**
- `look [-a|--all] [--at LIST] [--labs N] [--mode MODE] --snapshot FILE [--user-id ID] [--account-password-stdin|FILE] [--json]`
- `add --drug NAME --time TIME --measurement DOSE [--route ROUTE] [--mode MODE] [--extras k=v] --snapshot FILE ...`
- `dose list|edit|delete [--mode MODE] --snapshot FILE ...`
- `lab list|add|edit|delete [--mode MODE] --snapshot FILE ...`
- `weight get|set --snapshot FILE ...`
- `export --out FILE|- [--format sync|complete] --snapshot FILE ...`
- `import --in FILE [--format auto|sync|complete] [--replace] --snapshot FILE ...`

- [ ] **Step 1: Write failing command tests**

Use the fixture snapshot copied to a temp path: `look` shows a concentration and the recent lab; `add` changes the snapshot and a second `look` sees the event; `dose delete` writes a tombstone; `lab add/delete` and `weight set/get` work; `export --out -` is plaintext stdout; `export --out FILE` is encrypted and `import` merges it; `--replace` requires confirmation; malformed/oversized inputs fail with typed errors.

- [ ] **Step 2: Implement**

Add the snapshot/account-password flags, mode selection, drug/route/time parsers (accept the issue's `2026y10m19d` and ISO 8601), extras parsing, the `look` view model, and the merge/atomic-save path. Every write re-reads the current snapshot, merges, and writes atomically. Print `warning: uncalibrated baseline` unless a future calibration port lands.

- [ ] **Step 3: Run the full suite and commit**

---

### Task 8: Documentation and M2 verification

- [ ] **Step 1: Update `tools/hrt-tool/README.md` and the root README**: M2 status, `--snapshot` workflow, account password flags, export/import formats, calibration caveat, vector regeneration instructions.
- [ ] **Step 2: Clean rebuild + full `ctest` + manual `look`/`add`/`export`/`import` on a temp snapshot.**
- [ ] **Step 3: Verify hrt-cold/root scaffold untouched and commit.**

## M2 Completion Criteria

- PK vector tests pass for all 27 scenarios within `1e-7` relative tolerance.
- `look --snapshot` prints concentrations and recent labs; calibration is marked uncalibrated.
- `add`/`dose`/`lab`/`weight` read-merge-write the snapshot atomically; deletes tombstone.
- `export`/`import` support sync and complete formats; encrypted files are 0600 and plaintext never reaches a file.
- Wrong passwords/tampered snapshots fail closed with no output.
- The full suite passes; hrt-cold and the root scaffold are unchanged.
