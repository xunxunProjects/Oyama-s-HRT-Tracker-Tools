# hrt-tool

`hrt-tool` is the terminal client of the independent Oyama HRT Tracker Tools
project. It is a separate codebase from [`hrt-cold`](../hrt-cold/README.md):
different language, build, CI job, and release artifact.

**Status: M2 local data.** Implemented: the encrypted local vault, the
master-password entropy gate, encrypted debug logs, the CLI surface, URL/proxy
policy, the SPKI trust-store state machine, the libcurl transport, a typed
`ApiClient` stub, the C PK engine (31 reference scenarios), cloud-snapshot
decryption, sync-payload merge/tombstones, encrypted `HRTE` exports, the
complete-backup envelope, the Rust printer, and the local `look`/`add`/`dose`/
`lab`/`weight`/`export`/`import` commands. Still stubbed: the live
`/api/tool/v1/*` client and the trust prompts (M3/M4).

## Build

Dependencies: C11, C++17, CMake >= 3.16, OpenSSL >= 3.0, libcurl,
nlohmann/json, and a Rust toolchain (stable). OpenSSL 3 is required for
`EVP_MAC`, `OSSL_PARAM`, and the HKDF controls.

Debian/Ubuntu:

```sh
sudo apt-get install -y cmake pkg-config libssl-dev libcurl4-openssl-dev nlohmann-json3-dev
```

Build and test from the repository root:

```sh
cmake -S tools/hrt-tool -B tools/hrt-tool/build
cmake --build tools/hrt-tool/build
ctest --test-dir tools/hrt-tool/build --output-on-failure
```

The Rust printer is built by CMake with `cargo build --release --offline`. The
crate has no dependencies, so the offline build is reproducible.

## First run

```sh
tools/hrt-tool/build/hrt-tool init
tools/hrt-tool/build/hrt-tool config path
tools/hrt-tool/build/hrt-tool version
```

`init` prompts for a master password twice, without echo. The password must
reach at least **40 bits under both** the website's guess model
(`estimatePasswordBits`) and the uniform model `L x log2(N)`. The prompt prints
both numbers. A raw empirical Shannon value is available only as a diagnostic;
it is not a password-strength estimator.

The vault is written to `<config-dir>/vault.hrtv` with mode `0600` on Unix.
The config directory is `$XDG_CONFIG_HOME/hrt-tool` or `$HOME/.config/hrt-tool`
on Unix and `%APPDATA%\hrt-tool` on Windows.

Automation may supply the master password with `--vault-password-stdin` or
`--vault-password-file FILE` (mode `0600` on Unix). Environment variables and
command-line arguments are never used for secrets.

## Commands

| Command | Status |
| --- | --- |
| `version` | Implemented: tool, C core, and Rust printer versions. |
| `init [--reset]`, `config path`, `logs show --log-file FILE` | Implemented. |
| `look`, `add`, `dose`, `lab`, `weight`, `export`, `import` | Implemented against a local `--snapshot` (M2). |
| `login`, `logout`, `whoami` | Stub; the live API is M4. |
| `snapshot pull|push` | Stub; the live API is M4. |
| `config get`, `config set`, `logs <other>` | Stub; exit `6`. |

### Local snapshot workflow (M2)

The M2 commands work from a local snapshot file: either the website's
end-to-end-encrypted cloud bundle (`{cloud:1, iv, data}`) or a legacy plaintext
payload. An encrypted snapshot needs the account password and the account's
user id; both are used to derive the cloud key exactly as the website does.

```sh
# Read the snapshot and print concentrations plus recent labs.
hrt-tool look --snapshot snapshot.json --user-id <id> \
  --account-password-file account.txt

# Add a dose and write a new encrypted snapshot.
hrt-tool add --snapshot snapshot.json --user-id <id> \
  --account-password-file account.txt \
  --drug "Estradiol valerate" --time 2026y10m19d --measurement 2mg

# Export plaintext to stdout, or an encrypted HRTE file.
hrt-tool export --out - --snapshot snapshot.json --user-id <id> \
  --account-password-file account.txt
hrt-tool export --out backup.hrte --vault-password-file vault.txt \
  --snapshot snapshot.json --user-id <id> --account-password-file account.txt

# Import a sync payload or a complete-backup envelope (merge by default).
hrt-tool import --in backup.hrte --vault-password-file vault.txt \
  --snapshot snapshot.json --user-id <id> --account-password-file account.txt
```

`--account-password-file` and `--vault-password-file` must be mode `0600`.
The account password unlocks the cloud snapshot; the vault master password
encrypts `HRTE` export files. `import --replace` needs `--yes` (or a typed
`yes`) and replaces the whole snapshot; the default is the website's merge
rules with tombstones. An explicit import is the user asking for records back,
so it clears the matching local tombstones; records missing from the import
are not deleted.

Imports are sanitised the way the website sanitises them: unknown routes and
out-of-range event times are dropped (and counted in the output), doses are
clamped to `0..10000 mg`, weights to `20..400 kg`, invalid lab units default
to `pmol/l`, and records without an id get one. Payloads larger than 16 MiB
or with more than 20,000 records per array are refused. Naked v1 event arrays
are accepted alongside v1 flat objects and v2 `modes` payloads.

Mutating local commands take an advisory lock on `<snapshot>.lock` and hold it
across read-modify-write, so concurrent `add`/`import` processes cannot lose
an update. `look` and `export` do not need the lock and work in a read-only
directory. Exports refuse to overwrite an existing path.

The `look` numbers are the uncalibrated baseline: the website's EKF/OU/MIPD
calibration is not ported yet, and `look` prints a warning saying so.

`--time` accepts `now`, `+2h`/`-30m`, `2026y10m19d`, and ISO 8601 with an
optional `Z` or `+HH:MM`/`-HH:MM` offset; an explicit offset names the instant
it denotes regardless of `--utc`. Trailing junk is rejected.

Global flags (accepted before or after the command):

```text
--server URL  --proxy URL  --config-dir DIR  --token-file FILE
--debug  --trace  --json  --no-color  --yes  --timeout SECONDS
--trust-server  --insecure-tls  --transport auto|tls|openssl-v1
--no-cache-cloud-key  --compute local|server
--vault-password-file FILE  --vault-password-stdin  --log-file FILE
--snapshot FILE  --mode transfem|transmasc  --utc
--user-id ID  --account-password-file FILE  --account-password-stdin
```

## Vault format

All integers are big-endian. The header is 48 bytes and is also the AES-GCM
AAD, so a changed KDF parameter or salt fails authentication.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | magic `HRTV` |
| 4 | 2 | version `1` |
| 6 | 2 | KDF `1` (PBKDF2-SHA256) |
| 8 | 4 | iterations `600000` (accepted range 100000..1000000) |
| 12 | 2 | salt length `16` |
| 14 | 16 | salt |
| 30 | 2 | nonce length `12` |
| 32 | 12 | nonce |
| 44 | 4 | ciphertext length, including the 16-byte GCM tag |
| 48 | n | AES-256-GCM ciphertext and tag |

The plaintext is a JSON vault payload. Writes go to a mode-`0600` temporary
file, are `fsync`ed, and are renamed over the vault; a failed write leaves the
previous vault intact. A vault readable by group or others is refused.

The log key is `HKDF-SHA256(vault_key, "hrt-tool-log-v1", 32)`.

## Encrypted logs

`--debug`/`--trace` are parsed; in M1 `--debug` prints a short stub line to
stderr. Transport and TLS diagnostics require the live client and land in M4.
With `--log-file FILE`, each record is:

```text
u32 payload_len | u64 timestamp_ms | 12-byte nonce | AES-256-GCM ciphertext+tag
```

`payload_len` covers the timestamp, nonce, and ciphertext. The first 24 bytes
(length, timestamp, nonce) are the AAD. `hrt-tool logs show --log-file FILE`
decrypts the records to stdout. Plaintext logs are never written.

## Security model

- All local state is encrypted at rest: token, user id, device pass, cached
  cloud key, server/proxy settings, trust pins, and the OpSec acknowledgement.
- Plaintext is written only to stdout. `export --out FILE` writes an encrypted
  `HRTE` container; plaintext is available only with `--out -`. `import`
  accepts encrypted `HRTE` or plaintext input.
- `export --out FILE` refuses to replace an existing file, and all local reads
  are capped at 16 MiB after a regular-file check, so a FIFO or a growing file
  cannot exhaust memory.
- Login sends the account password to the server, exactly as the website does.
  The cloud key is derived from that password and the user id, so a
  compromised worker or request log can derive the key. The "server cannot read
  health data" property holds for an honest-but-curious server that follows the
  protocol; it does not survive a compromised endpoint. Do not reuse a password
  from another service.
- The tool does not call `system`, `popen`, `exec`, `dlopen`, or a shell, and
  never executes or evaluates server-provided content. Server strings are data.
- HTTPS is required for ordinary remote servers. Plain `http://` is accepted
  only for loopback, `.onion`, and `.i2p` hosts (policy), and the M4 trust
  prompt will require an explicit decision before first contact.
- Trust pins the SPKI SHA-256, not the leaf certificate, so certificate
  renewals do not break the client. M1 implements the trust-store state
  machine; live pin enforcement lands with the real client in M4.
- Automatic redirects currently follow libcurl's default; the M4 live client
  will replace them with a re-validated redirect loop and cap response
  headers alongside the body. The M1 `ApiClient` stub opens no socket, so the
  transport path is not reachable yet.
- `logs show` requires `--log-file FILE` in M1; a default log location in the
  encrypted vault is M4 work.
- Windows relies on NTFS ACLs rather than Unix mode bits; the Windows build
  and ACL verification are M4 work, and the Windows vault write is not yet
  crash-durable (`fsync` is POSIX-only here).
- Tor/I2P are reached through `--proxy socks5h://127.0.0.1:9050` (Tor) or
  `socks5h://127.0.0.1:4447` (I2P). `socks5h` keeps DNS at the proxy.
- OpSec is the user's responsibility: data can be correlated with the account,
  a malicious server can return poisoned records or a crafted token, and the
  network path does not protect the endpoint or the person.

This is an engineering checkpoint with tests, not a security audit and not a
production support claim.

## Layout

```text
tools/hrt-tool/
  CMakeLists.txt              standalone CMake project
  include/hrtcore/            C core ABI
  native/core/                OpenSSL crypto primitives
  src/                        C++ CLI, vault, entropy, logs, HTTP, trust, API
  rust/hrt-print/             Rust printing static library
  tests/                      C/C++ tests
  advanced/gpg_f2f/           documentation-only Python placeholder
```

`hrt-cold`, the root `CMakeLists.txt`, `include/hrtcold/`, and `tests/native/`
are not part of this build and are not modified by it.

## PK reference vectors

`testdata/pk-vectors.json` is generated from the website's `logic.ts` with
vitest. To regenerate it, copy a generator test into the website repository's
`tests/` directory, run it with `TZ=UTC` and the website's `vitest`, and merge
the result. The C engine is validated against all 31 scenarios (injection,
oral, sublingual, gel, patch, CPA, T, multi-dose, weight, custom params) at
`1e-7` relative tolerance, plus the grid endpoints and AUC. The patch cases
include three overlapping patches with distinct release rates, same-time
applies, the newest-group fallback, and planned/overwear removals, so the
pairing order and the open-list removal semantics are pinned to `logic.ts`.

## Roadmap

- **M2 (implemented):** local PK model, snapshot decryption, and
  `look`/`add`/`dose`/`lab`/`weight`/`export`/`import`.
- **M3:** add `/api/tool/v1/*` to the website worker on branch
  `Tool-Interface/data`, with tests, wait for green CI, then PR and merge as the
  final step.
- **M4:** wire the live client, SPKI trust prompts, Tor/I2P, encrypted request
  envelopes, and integration tests against local `wrangler dev`.
- Later: calibration (EKF/OU/MIPD), optional server compute, and the GPG F2F
  advanced mode.

## Licence

The repository is MIT.
