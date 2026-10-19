# hrt-tool — Design

**Date:** 2026-10-19
**Status:** proposed; awaiting owner approval
**Owner decisions:** recorded below; anything under "Builder calls" is mine, not the owner's.

## 1. Intent

Give the Oyama HRT Tracker a terminal client that works when opening the web app
is slow or impractical — Tor/I2P deployments, headless machines, scripts, and
low-bandwidth links. The client can log in, pull the account's encrypted
snapshot, decrypt it locally, compute the same concentration curve the website
shows, add doses/labs/weight, and upload a new encrypted snapshot.

The server is a transport for encrypted snapshots and updates. It does not
compute concentrations and does not store plaintext health data. It does
receive the account password at login (the existing website flow) and could
therefore derive the cloud key if the worker or its logs were compromised;
see Section 10.

This is the second tool in the independent Tools project. `hrt-cold` remains the
offline OpenPGP cold-backup CLI and is not reworked.

## 2. Owner decisions

These came from Issue #131, its comments, and the follow-up interview:

- Official tool name: **hrt-tool**, in `xunxunProjects/Oyama-s-HRT-Tracker-Tools`.
- Commands shown in the issue: login like `git`/`gh`, `--look -all`, `--add`,
  `--proxy socks5://127.0.0.1:9050` (Tor) and I2P's 4447, a 7-day token, and
  `--debug`/`--trace` for TLS logs. TLS logs must not print by default.
- Transport: TLS/SSL preferred. Onion/I2P servers are untrusted by default and
  require a one-time explicit user `yes`. An openssl-compatible encrypted body
  layer (AES-256-CBC + HMAC-SHA256) is available; an exact
  `openssl enc -aes-256-cbc -a -salt -pbkdf2` mode is legacy-compatible only.
- Computation is **local first**. The server only carries historical data and
  accepts updates. A server compute endpoint is reserved in documentation but
  not implemented in v1.
- Data contracts: **both**. Live commands read and write the website's v2 sync
  payload (what `/api/content` stores). `export`/`import` additionally accept
  the `oyama-hrt-complete-backup` envelope.
- Import and delete: merge by default using the website's rules; `--replace`
  is explicit and confirmed. Deletes write tombstones; the CLI never removes
  an array element on delete.
- Language layering: **C** is the low-level library, **C++** sits on it, and
  **Rust** is the printing/output layer. Rust exists to keep rendering
  memory-safe and bounded (no print-path OOM).
- GPG over F2F is **not implemented in v1**. Reserve a Python placeholder with
  comments; F2F means face-to-face public-key exchange. Keep the codebase small
  for other maintainers.
- Server-side interface: new versioned `/api/tool/v1/*` namespace in the main
  repository, on branch `Tool-Interface/data`, PR'd to the upstream project.
- Every piece of local state, including logs and exports, is encrypted at
  rest. The CLI is unlocked with a master password on each run; the password
  must reach at least 40 bits under the project's entropy estimate.
- The two tools are independent codebases: hrt-cold (Go) and hrt-tool
  (C/C++/Rust) share no runtime source, build, CI job, or release artifact.
- Delivery: main-repo changes go through a PR; all CI checks must be green
  before merge, and merging to `main` is the final step. Placeholders may be
  committed to the feature branch, but each one must build and run; fix
  failures immediately.

## 3. Goals

1. `hrt-tool login` / `logout` / `whoami` with a stored 7-day session token.
2. `hrt-tool look -all` showing the modeled concentration now and at
   1m, 5m, 10m, 30m, 1h, 2h, 4h, 8h, 12h, 24h, 2d, plus recent labs.
3. `hrt-tool add` (and edit/delete) for dose events, labs, and weight; the
   change is written back as a new encrypted snapshot revision.
4. Local decryption and local PK simulation. No plaintext health data is sent
   to the server in v1.
5. SOCKS5/HTTP proxy support, including Tor (`socks5h://127.0.0.1:9050`) and
   I2P (`socks5h://127.0.0.1:4447`).
6. First-contact trust pinning and explicit OpSec warnings.
7. `--debug`/`--trace` diagnostics with secrets redacted; silent by default.
8. Bounded memory and deterministic failure behavior on every input path.
9. Support both the cloud sync payload and the complete-backup envelope on
   export/import, reporting categories the cloud cannot carry.
10. Honor the snapshot's `pkParams` custom PK overrides.
11. Encrypt all local state, logs, and file exports at rest; decrypt into
    memory only for the duration of a command and zeroize afterwards.
12. Enforce a master password with at least 40 bits of estimated entropy.

## 4. Non-goals

- Reimplementing the website UI, sharing, admin, TOTP setup, passkeys, or
  account deletion.
- Replacing `hrt-cold` or re-implementing `.hrtb`.
- Server-side computation in v1.
- GPG/F2F key exchange in v1.
- A global freshness service or remote attestation.
- Claiming the model is clinically validated.

## 5. Anti-goals

- Sending plaintext doses/labs to the worker.
- Executing anything the server returns (no shell, no eval, no `popen`,
  no dynamic code loading, no server-supplied file paths).
- Auto-trusting a new host, an onion/I2P host, or a changed certificate.
- Printing Authorization headers, passwords, tokens, cloud keys, or request
  bodies by default.
- Unbounded allocations driven by server responses or snapshot sizes.
- Silently overwriting an existing export or snapshot file.
- Writing tokens, cloud keys, logs, exports, or snapshots in plaintext to a
  regular file. Plaintext is only ever written to stdout (`--out -`).
- Sharing runtime code, build files, CI jobs, or release artifacts between
  hrt-cold and hrt-tool.

## 6. Architecture

```
┌──────────────────────────── hrt-tool (C++ executable) ────────────────────────────┐
│ CLI parsing · config/token store · HTTP(S) via libcurl · JSON via nlohmann        │
│ protocol/orchestration · trust store · redacted debug/trace                       │
└───────────────┬───────────────────────────────────────────────┬───────────────────┘
                │ C ABI (fixed buffers, explicit lengths)        │ C ABI (JSON + format)
┌───────────────▼──────────────────────┐          ┌─────────────▼──────────────────┐
│ hrtcore (C, OpenSSL)                 │          │ hrtprint (Rust, static library) │
│ PBKDF2/AES-GCM/AES-CBC+HMAC/base64   │          │ table/JSON/error rendering      │
│ PK simulation, interpolation         │          │ bounded input, streaming output │
│ no I/O, no globals, caller buffers   │          │ no network, no files            │
└──────────────────────────────────────┘          └─────────────────────────────────┘
```

- **C core (`hrtcore`)** owns only pure computation: PK math and crypto
  primitives. It performs no network, file, or shell I/O, keeps no mutable
  global state, and takes caller-provided output buffers with explicit
  capacities. Every entry point validates lengths and returns a typed error.
- **C++ layer** owns argument parsing, configuration, token storage, HTTP,
  JSON, protocol version negotiation, trust decisions, and calls into the C
  core. RAII wrappers keep C resources and buffers bounded.
- **Rust (`hrtprint`)** owns rendering only. It receives a compact JSON view
  model, never the full simulation grid, and writes to stdout/stderr. Input and
  output are capped (16 MiB each); invalid or oversized input fails closed.
- **Python** is not used by the v1 runtime. `advanced/gpg_f2f/` holds a
  documented placeholder only.

The website's `logic.ts` remains the reference implementation. The C port is
validated against vectors generated from it (Section 11).

## 7. Repository layout

```text
tools/hrt-tool/                    standalone project; nothing shared with hrt-cold
  CMakeLists.txt                   its own build; invokes cargo for hrtprint
  configure                        optional convenience wrapper
  include/hrtcore/hrtcore.h        C core ABI (PK, crypto, vault, entropy)
  include/hrtprint/hrtprint.h      Rust printing ABI
  native/core/*.c                  C core implementation
  native/core/*_test.c             C unit tests
  native/vectors/*.json            frozen PK test vectors from logic.ts
  src/main.cpp                     entry point
  src/cli.*                        argument parsing and command dispatch
  src/vault.*                      encrypted-at-rest config/token/key store
  src/entropy.*                    password entropy gate (website model)
  src/config.*                     paths and non-secret defaults
  src/http.*                       libcurl transport, proxy, TLS info, trace
  src/api.*                        /api/tool/v1 client (interface + stub first)
  src/snapshot.*                   encrypted snapshot envelope handling
  src/backup_envelope.*            complete-backup validation and conversion
  src/model.*                      JSON <-> C structs, snapshot merge helpers
  src/log.*                        encrypted debug/trace records
  src/commands/*.cpp               init/login/look/add/lab/weight/export/...
  rust/hrt-print/                  cargo staticlib, C ABI
  tests/                           C++ unit tests, mock server, CLI tests
  advanced/gpg_f2f/                Python placeholder + README (not built)
  scripts/build-release.sh
  README.md
docs/tool-interface/TOOL_API.md    server contract, copied to the main repo
```

`hrt-cold` and its Go module, root `CMakeLists.txt`, `include/hrtcold/`, and
`tests/native/` stay untouched. hrt-tool has its own CMake project, CI job,
and release script. The two tools share documentation only.

## 8. CLI surface

Global flags:

```
--server URL            default https://hrt.mahiro.uk (configurable)
--proxy URL             socks5h://, socks5://, http://, https://
--config-dir DIR        default $XDG_CONFIG_HOME/hrt-tool or %APPDATA%\hrt-tool
--token-file FILE       override token location
--debug                 request/status/TLS summary, secrets redacted
--trace                 libcurl verbose lines, secrets redacted
--json                  machine-readable output (stable schema)
--no-color              disable ANSI styling
--yes                   non-interactive confirmation (never auto-trusts)
--timeout SECONDS       per-request timeout (default 30)
--trust-server          persist a first-contact SPKI trust decision
--insecure-tls          allow an invalid certificate (still requires trust)
--transport auto|tls|openssl-v1   default auto
--no-cache-cloud-key    do not cache the derived cloud key
--compute local|server  default local; `server` is reserved and errors in v1
--vault-password-file FILE   read the master password from a 0600 file
--vault-password-stdin       read the master password from stdin
--log-file FILE              write encrypted debug/trace records
```

Token lifetime is 7 days. The server revokes a session after 3 days without a
request, so `whoami` refreshes `last_used_at`; an expired or revoked session is
reported distinctly and requires `login` again.

Commands (v1):

| Command | Purpose |
| --- | --- |
| `init` | Create the local vault and set the master password; refuse below 40 bits. |
| `login` | Password/TOTP/backup-code login; stores token, user id, device pass. Prompts without echo. |
| `logs show [--log-file FILE]` | Decrypt encrypted debug/trace records to stdout. |
| `logout [--all] [--forget-key]` | Revoke the current session (or all), optionally forget the cached cloud key. |
| `whoami [--json]` | Verify the token and show server/user/token expiry. |
| `look [-a\|--all] [--at LIST] [--labs N] [--mode MODE] [--snapshot FILE]` | Pull (or read offline), decrypt, simulate, print current concentration and offsets. |
| `add --drug NAME --time TIME --measurement DOSE [--route ROUTE] [--mode MODE] [--extras ...]` | Append a dose event and upload a new snapshot. |
| `dose list\|edit\|delete [--mode MODE]` | Manage dose events; delete writes a tombstone. |
| `lab list\|add\|edit\|delete [--mode MODE]` | Manage lab results; delete writes a tombstone. |
| `weight get\|set` | Manage body weight. |
| `export --out FILE [--format sync\|complete]` | Write the payload encrypted at rest (0600, no overwrite). `sync` is the cloud payload; `complete` is the envelope, with absent categories marked as not mentioned. `--out -` writes plaintext to stdout only. |
| `import --in FILE [--format auto\|sync\|complete] [--replace]` | Accept encrypted or plaintext input; validate, merge by default with the website's rules, or replace with explicit confirmation. Report categories the cloud cannot carry. |
| `snapshot pull --out FILE` | Save the encrypted snapshot as received. Refuses a plaintext legacy snapshot to a file; `--out -` is stdout only. |
| `snapshot push --in FILE` | Upload an encrypted snapshot unchanged. |
| `config get\|set\|path` | Inspect or change server/proxy/trust settings. |
| `version` | Tool, C core, Rust printer, API, and protocol versions. |

`look` output (default, human):

```
Server   https://hrt.mahiro.uk   mode transfem   weight 70 kg
Now      2026-10-19 22:50 UTC+08:00

Drug                 Route      Last dose        Now        +1m    +5m   +10m   +30m     +1h ...
Estradiol valerate   injection  1d 11h 10m  169 pg/mL    165    160    155    140      ...
Cyproterone acetate  oral       10h 05m      0.12 ng/mL   ...

Recent labs:
  2026-10-18 09:00  Estradiol      xxx pmol/L (xxx pg/mL)  LC-MS/MS
  2026-10-18 09:00  Testosterone   xxx ng/dL (xxx nmol/L)
```

- `--all` includes every active compound and every default offset.
- `--at` accepts `1m,5m,10m,30m,1h,2h,4h,8h,12h,24h,2d` or custom values.
- `--json` is a stable object: `{now, mode, weightKg, compounds:[...], labs:[...], warnings:[...]}`.
- If the stored calibration method is not `off` and the C port has not
  implemented it (v1), print a clear warning that the curve is uncalibrated.

`add` accepts the issue's spelling and common variants:

```
hrt-tool add --drug "Estradiol valerate" --time 2026y10m19d --measurement 2mg
hrt-tool add --drug EV --route injection --time 2026-10-19T22:50 --dose 2mg
```

- Drug names map to ester codes (EV, EB, EC, EN, EU, E2, CPA, T, TC, TE, TU,
  SPIRO, BICA, MPA, LEUP, TRIP, RLX). Unknown codes pass through as unmodelled
  records, matching the website.
- Routes: sublingual, injection, patchApply, patchRemove, gel, oral.
- If `--route` is omitted, use an unambiguous default per drug and print the
  assumption; ambiguous drugs require `--route`.
- Times accept `YYYYyMMmDDd`, ISO 8601, `now`, and relative `-90m`/`+2h`.
  Naive times use the local zone unless `--utc`.
- `--extras key=value` covers concentrationMGmL, areaCM2,
  releaseRateUGPerDay, sublingualTheta, sublingualTier, gelSite, patchWearH.

## 9. Server interface — `/api/tool/v1/*`

All routes are under the existing Cloudflare Worker. Authentication is the
existing 7-day JWT (`Authorization: Bearer`). CORS remains `*`, matching the
current worker; the CLI does not use cookies.

| Method | Path | Auth | Purpose |
| --- | --- | --- | --- |
| GET | `/version` | no | `{apiVersion, minToolVersion, transport[], serverTime}` |
| POST | `/login` | no (TLS) | Wraps the existing login flow; adds `apiVersion`, `serverTime`, `transport[]`. |
| GET | `/me` | yes | `{user, serverTime, apiVersion}`; token validity check. |
| GET | `/snapshot` | yes | Latest stored snapshot `{id, createdAt, data}` or 404 `NO_SNAPSHOT`. |
| GET | `/snapshots` | yes | Metadata list `[{id, createdAt, dataSize}]`. |
| GET | `/snapshot/:id` | yes | One snapshot by id. |
| PUT | `/snapshot` | yes | Store `{data}` as a new revision; same size cap and retention policy as `/api/content`. Accepts `If-Match: <baseRevisionId>`. |
| POST | `/logout` | yes | Delete the session row named by the token's `sid`. |
| POST | `/compute` | — | **Reserved, not implemented in v1.** Returns 501 `NOT_IMPLEMENTED`. Documented for a future opt-in server compute. |

Semantics:

- `login` reuses the existing per-IP/per-account rate limits, lockout, device
  pass, TOTP, and backup-code behavior. It does not introduce a second auth
  implementation.
- `snapshot` reads and writes the same `content` rows as `/api/content`; the
  retention policy is unchanged. The `data` payload is the E2E-encrypted
  `{cloud:1, iv, data}` bundle produced by the website, or a legacy plaintext
  bundle. The worker treats it as opaque.
- The stored payload is the website's v2 sync payload. The complete-backup
  envelope is a local file format; the server never stores it.
- `PUT /snapshot` accepts `If-Match: <baseRevisionId>` (also
  `X-HRT-Base-Revision`). If supplied and the newest retained revision differs,
  return 409 `CONFLICT` with the current newest metadata. Without it, the write
  is last-writer-wins, and the CLI must run the website's optimistic loop:
  read newest, merge, attempt conditional write, refetch and retry on 409.
- Deletes are tombstones (`deletions.events[id]` / `deletions.labResults[id]`)
  with an `updatedAt`, matching `syncMerge.ts`; the CLI never removes an array
  element on delete.
- `snapshot` writes use the existing `content-write:<userId>` rate limit.
- `logout` only deletes the caller's own session; a missing `sid` is a no-op.
- The worker never decrypts the snapshot, never derives the cloud key, and
  never computes concentrations in v1.

Implementation shape in the main repo:

- Add `toolApi.ts` for `/api/tool/v1/*` routing, transport-envelope handling,
  and shared helpers.
- Extract the existing login handler and content read/write/retention logic
  into functions that both the old and new routes call. Do not fork the
  behavior or duplicate the SQL.
- Keep the existing `/api/login` and `/api/content` responses byte-compatible.
- Add `docs/tool-interface/TOOL_API.md` (contract) and a matching copy in the
  Tools repository.
- Add `tests/toolInterface.test.ts` using the existing worker test harness.

## 10. Transport, trust, and OpSec

### TLS and proxies

- Plain HTTPS is the baseline. Non-loopback servers must use `https://`.
- libcurl provides HTTP/1.1/2, TLS, certificate verification, and proxies.
- `--proxy socks5h://127.0.0.1:9050` is the Tor example; `socks5h://…:4447`
  is the I2P SOCKS example. Remote DNS (`socks5h`) avoids local leaks.
- `--insecure-tls` is allowed only with an explicit trust decision and prints
  a warning on every run.

### First-contact trust

- Pin the **SPKI** (Subject Public Key Info) SHA-256, not the leaf
  certificate. Leaf pinning would fail on every Cloudflare certificate
  renewal; SPKI survives renewal and still catches a key change.
- On the first connection to an origin, show scheme/host/port, certificate
  subject/issuer, validity, and the SPKI digest. Require interactive `yes` or
  `--trust-server`. `--trust-server` persists the decision; `--yes` only
  answers prompts for the current run and never silently pins a new or
  changed key.
- Persist `{origin, spkiSha256, firstSeen, lastSeen}` in
  `known_servers.json` (0600). A changed SPKI refuses the connection until the
  user re-trusts it with `--trust-server`.
- Onion/I2P origins are never auto-trusted and show a stronger warning.
- Under `socks5h`, DNS is resolved by the proxy; show "resolved by proxy"
  instead of pretending to know a local address.
- Plain `http://` is allowed only for loopback and `.onion`/`.i2p` hosts, only
  with an explicit trust decision, and prints a warning on every run. Other
  public hosts must use `https://`.
- `--insecure-tls` permits an invalid certificate only after the same explicit
  trust decision, and prints a warning on every run.

### Transport body envelope (optional, default `auto`)

- `--transport=auto` (default): use the envelope when `/version` advertises
  `openssl-v1`; otherwise plain JSON over TLS.
- `--transport=openssl-v1`: force the authenticated envelope.
- `--transport=tls`: disable the envelope.
- The exact `openssl enc -aes-256-cbc -a -salt -pbkdf2` compatibility mode was
  dropped: neither repository has an existing artifact it must interoperate
  with, and an unauthenticated CBC mode is strictly weaker. The primitives
  remain OpenSSL's; the envelope adds HMAC authentication.
- Envelope (`openssl-v1`):
  - `K = PBKDF2-SHA256(token, salt, 10000, 64)`; `Kenc` = first 32 bytes,
    `Kmac` = last 32 bytes. The high-entropy token makes 10k rounds
    acceptable; the count is documented and versioned.
  - `ct = AES-256-CBC(Kenc, iv, plaintext)` with PKCS#7.
  - `mac = HMAC-SHA256(Kmac, "hrt-tool-transport-v1\0" || v || salt || iv || ct)`.
  - Base64 armor. The MAC is verified in constant time before decryption.
  - Responses use a fresh IV and the same keys.
- The existing cloud snapshot encryption (PBKDF2-SHA256 600k + AES-256-GCM)
  is unchanged and independent of the transport layer.
- The transport key is derived from the session token, so the token is visible
  to whoever terminates TLS. This layer protects health payloads from
  TLS-terminating logs/CDNs, not the bearer token itself. Document this.

### OpSec warnings

Printed once per config (and always in `--help`/README):

- Data can be correlated with the account and with other posts; this tool is
  not anonymity software.
- A malicious or compromised server can return poisoned records or a crafted
  token. The tool treats all server data as data: it never executes it, never
  passes it to a shell, and never uses it as a path or command.
- Login sends the account password to the server, exactly as the website does.
  The cloud key is derived from that password and the user id, so a
  compromised worker or request log can derive the key. The "server cannot
  read health data" claim holds for an honest-but-curious server that follows
  the protocol; it does not survive a compromised endpoint. Avoid reusing a
  password from anywhere else.
- Keep the token, device pass, and cloud key private. Use `logout --forget-key`
  on shared machines.
- Tor/I2P protect the network path, not the endpoint, the account, or the
  person.

## 11. PK model port

Reference: `logic.ts` (`runSimulation`, `resolveParams`,
`PrecomputedEventModel`, `computeMaxLifetimeH`, interpolation helpers).
The port must be bit-for-bit faithful within floating-point tolerance, not a
new model.

v1 scope:

- Routes: injection, oral, sublingual, gel, patchApply, patchRemove.
- Compounds: E2/EB/EV/EC/EN/EU, CPA, T/TC/TE/TU; unmodelled drugs are
  filtered exactly as the website does.
- `runSimulation` equivalent with a caller-supplied `nowH` so results are
  reproducible; the CLI passes the current time.
- `interpolateConcentration_E2/CPA/T`.
- Per-event extras: concentrationMGmL, areaCM2, releaseRateUGPerDay,
  sublingualTheta/sublingualTier, gelSite, patchWearH.
- Default PK parameters and the same clamp/sanitize bounds.
- `pkParams` from the snapshot, through the same sanitizer and override
  path as `sanitizePKParams` / `applyPKOverrides`. Invalid values fail closed
  or fall back exactly as the website does; they are never silently replaced
  by defaults when the snapshot says otherwise.

Deferred to a later milestone (explicitly warned in `look`):

- `computeCalibration` methods `ekf`, `ou_kalman`, `mipd`, and
  `historyMode=forward`. v1 runs the uncalibrated baseline.
- Calibration method/history mode are **not** part of the cloud sync payload;
  they live in the website's localStorage. A live `look` therefore cannot see
  them, and v1 prints a standing notice that the curve is uncalibrated. A
  complete-backup envelope that carries a `calibration` block is read and
  reported as unsupported in v1 rather than silently ignored.
- `--compute server` and `/api/tool/v1/compute`.

### Data contracts and merge rules

- Live commands use the website's v2 sync payload:
  `{meta:{version:2}, mode, modes:{transfem,transmasc}, weight, weightUpdatedAt,
  pkParams, pkParamsUpdatedAt}` plus the flat v1 mirrors the website writes.
- `export --format sync` writes that payload. `export --format complete`
  writes an `oyama-hrt-complete-backup` envelope: the mode blocks are carried,
  `weight`/`pkParams` are carried when present, and categories the cloud
  payload does not represent (`quickDoses`, `supplies`, `plan`, preferences,
  calibration) are omitted (the envelope spec treats omitted arrays as "not
  mentioned", not as empty).
- `import --format auto` detects the envelope by its `format` field. Both
  formats merge by default using the website's rules: new id adds, same id
  with reliable `updatedAt` lets the newer win, conflicts abort the category,
  tombstones are unioned, and a restored record clears its own tombstone.
  `--replace` writes the whole payload after a second confirmation.
- Envelope-only categories imported from a complete-backup file are reported
  as "not carried by the cloud sync payload"; the tool never claims they were
  synchronized. Nothing is dropped silently.
- Mode: `add`/`lab` write to the snapshot's active `mode`; `--mode` overrides.
  If the active mode is absent, the command requires `--mode` rather than
  guessing.

Test vectors:

- A generator script imports `logic.ts` from a main-repo checkout
  (`HRT_SITE_DIR`) and writes frozen JSON vectors for:
  each route × representative esters, multi-dose histories, extras/patch
  removal, T and CPA curves, AUC/Tmax/half-life cases, and query times.
- Vectors are committed. CI does not need the main repo or Node.
- C tests assert within `1e-9` relative error where arithmetic order allows,
  and `1e-6` otherwise; the exact tolerance is recorded per vector.
- Additional property tests mirror `tests/routes.test.ts` expectations
  (AUC = F·D/k, Tmax ordering, half-life).

## 12. Safety, at-rest encryption, and resource limits

### Local vault

All persistent client state lives in one encrypted vault:
`<config-dir>/vault.hrtv` (0600 on Unix). It contains the session token,
user id/name, device pass, cached cloud key, server/proxy settings, trust
pins, and OpSec acknowledgement. No secret is ever written outside it.

- First run: `hrt-tool init` prompts for a master password twice. Every other
  command unlocks the vault first; `version` and `--help` do not.
- KDF: PBKDF2-SHA256, 600,000 iterations, 16-byte random salt, 32-byte key
  (the same KDF the website already uses). AES-256-GCM with a 12-byte nonce;
  the header (magic `HRTV`, version, KDF id, iteration count, salt) is the
  AAD, so KDF parameters cannot be downgraded silently.
- A wrong master password fails the GCM tag; there is no separate verifier.
- Unlock decrypts into memory, the command uses it, and the buffer is
  zeroized before exit. The master password is never stored.
- Writes are atomic: temp file in the config directory, fsync, rename over the
  vault. A failed write leaves the old vault intact.
- Automation may pass the password with `--vault-password-file` (0600) or
  `--vault-password-stdin`; environment variables and argv are not accepted.
- `logout --forget-key` removes the cached cloud key; `init --reset` replaces
  the vault only after an explicit confirmation.

### Entropy gate

The master password must reach **both** of these, each at least 40 bits:

- the website's guess-model estimate (`estimatePasswordBits`, whose weak
  threshold is already 40), and
- the uniform Shannon model `L x log2(N)` (`theoreticalPasswordBits`).

The setup prompt prints both numbers and the character classes used. A raw
empirical Shannon calculation over one string is shown only under `--debug`
as a diagnostic; it is not a password-strength estimator and is not the gate.
The account password is still validated by the server; the local gate is
only for the vault.

### Encrypted logs and exports

- `--debug`/`--trace` go to stderr by default. Nothing is written to disk
  unless `--log-file FILE` is set.
- With `--log-file`, each record is `u32 length | u64 timestamp | 12-byte
  nonce | AES-256-GCM ciphertext+tag`, keyed by
  `HKDF-SHA256(vault_key, "hrt-tool-log-v1")`. `hrt-tool logs show` decrypts
  to stdout. Plaintext logs are never written.
- `export --out FILE` writes an encrypted container (`HRTE`, same KDF/AEAD as
  the vault). Plaintext is available only on stdout with `--out -`. There is
  no flag that writes plaintext to a regular file.
- `snapshot pull` writes the cloud snapshot, which is already AES-256-GCM
  encrypted. A legacy plaintext snapshot is refused for file output; stdout
  only.

### Resource limits

- Snapshot size: 2 MiB request cap (existing), 16 MiB local parse cap.
- Events per simulation: 100,000; labs: 10,000; query points: 1,000.
- The C core rejects NaN/Inf, negative doses, out-of-range times, and
  out-of-range weights rather than clamping silently unless the website does.
- No `system`, `popen`, `exec`, `dlopen`, or shell invocation anywhere.
- Server-provided strings are validated as JSON values; URLs are parsed and
  scheme-checked; redirects are limited and re-checked against the trust
  store.
- Output is capped at 16 MiB; oversized rendering fails closed.
- Files are written atomically. Exports never overwrite an existing path.
- Unix secret files are created 0600; Windows relies on NTFS ACLs and says so.

## 13. Build, tests, and CI

Dependencies: C11, C++17, CMake ≥ 3.16, libcurl, OpenSSL, a Rust toolchain
(staticlib), `nlohmann/json` (header-only, vendored or fetched at configure
time with a pinned hash).

- Linux: `cmake` configure/build + `ctest` + `cargo test`.
- Existing native header checks remain.
- New CI job `.github/workflows/tools-hrt-tool.yml`: Ubuntu first; Windows
  with MSVC/vcpkg as a follow-up job. The Go `hrt-cold` job is unchanged.
- Tests:
  - C: PK vectors, crypto round-trip, tamper/wrong-key, size limits.
  - C++: CLI parsing, config permissions, SPKI trust pinning and rotation,
    URL/proxy handling, redaction, snapshot envelope, both data contracts,
    merge/tombstone behavior, `If-Match` retry, mock-server integration.
  - Rust: golden tables/JSON, invalid/oversized input, no panics.
  - Main repo: worker route tests, transport envelope tests, login/lockout
    reuse, retention behavior, 401/session revocation, conditional snapshot
    writes (200/409), and both-format import conversion.

## 14. Milestones

1. **M1 — skeleton (Tools).** Branch `feat/hrt-tool`; standalone
   CMake/Cargo layout under `tools/hrt-tool/`; C core crypto/base64/error ABI;
   encrypted vault + entropy gate + encrypted logs; C++ config/token/trust;
   libcurl transport; Rust printer; commands wired to an `api` interface whose
   network methods are stubbed. Tests for everything that does not need the
   server.
2. **M2 — local data (Tools).** PK port + vectors; snapshot decrypt; `look`,
   `add`, dose/lab/weight edits, export/import against local files. No real
   server calls yet.
3. **M3 — server interface (main repo).** Fetch `upstream/main` (the local
   `main` is behind it) and branch `Tool-Interface/data` from that commit;
   `/api/tool/v1/*`, tests, docs; PR to
   `xunxunProjects/Oyama-s-HRT-Tracker:main`.
4. **M4 — wire and finish (Tools).** Implement the live `api` client, trust
   pinning, proxy/Tor/I2P, integration tests, README/HANDOFF/CHANGELOG,
   release packaging. Calibration and GPG stay deferred.

The owner's requested order is M1 → M2 → M3 → M4. The server interface is
left empty in M1/M2 exactly so M3 can land without reworking the CLI surface.

### Delivery rules

- The main repository is never committed to directly on `main`. Work goes on
  `Tool-Interface/data`, then a PR, then all CI checks green, then merge as
  the final step.
- Placeholders may be committed to the feature branch immediately, but each
  commit must build and run. Test after every placeholder; if it fails, fix
  it before continuing (the owner's "fix early" rule).
- The Tools repository uses `feat/hrt-tool`; `hrt-cold` stays untouched.
- A failed build or test blocks the next step; do not accumulate broken
  scaffolding.

## 15. Builder calls

These are my decisions, not owner requirements; they can be revisited:

- C++ executable with a Rust static library, rather than a Rust executable.
  "Rust only prints" is implemented literally.
- `nlohmann/json` and `libcurl` rather than hand-written JSON/HTTP.
- PK calibration and server compute are deferred; v1 warns instead of
  producing silently different numbers.
- `transport=auto` uses the authenticated envelope when advertised. The exact
  `openssl enc` mode was dropped: there is no existing artifact to
  interoperate with, and unauthenticated CBC is strictly weaker.
- Trust pins SPKI, not the leaf certificate, so Cloudflare renewals do not
  break the client. `--trust-server` persists; `--yes` does not.
- `http://` is allowed only for loopback and `.onion`/`.i2p` hosts after an
  explicit trust decision; all other public hosts require HTTPS.
- The v1 privacy statement is honest-server only: login sends the password,
  so a compromised worker can derive the cloud key. This is documented, not
  hidden.
- Both data contracts are supported, but the cloud sync payload cannot carry
  envelope-only categories; import/export report them instead of pretending.
- Deletes write tombstones; conditional `If-Match` writes prevent lost
  updates when the website and CLI write concurrently.
- Windows CI follows Linux; v1 is developed and verified on Linux first.
- `look` interpolates the model at the requested offsets instead of dumping
  the full simulation grid to the printer.
- The tool caches the derived cloud key by default inside the encrypted vault,
  with `--no-cache-cloud-key` and `logout --forget-key` to opt out.
- The entropy gate uses both the website's guess model and the uniform
  Shannon model, each at least 40 bits. Raw empirical Shannon entropy over a
  single string is diagnostic only.
- The vault uses PBKDF2-SHA256 600k + AES-256-GCM with the header as AAD.
- Logs are per-record AES-256-GCM under an HKDF-derived log key; a shared
  append-only file is never plaintext.
- hrt-tool is its own standalone CMake project under `tools/hrt-tool/`; it
  does not add targets to the root `hrtcold` CMake project.
- M4 integration tests run against a local `wrangler dev` with local D1, not
  production. Production verification is a separate, owner-approved step.
- Every placeholder or scaffold commit is built and run before the next
  step; failures are fixed immediately.
