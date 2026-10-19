# hrt-tool

`hrt-tool` is the terminal client of the independent Oyama HRT Tracker Tools
project. It is a separate codebase from [`hrt-cold`](../hrt-cold/README.md):
different language, build, CI job, and release artifact.

**Status: M1 skeleton.** Implemented: the encrypted local vault, the
master-password entropy gate, encrypted debug logs, the CLI surface, URL/proxy
policy, the SPKI trust-store state machine, the libcurl transport, and a typed
`ApiClient`. Not implemented yet: the live `/api/tool/v1/*` client, the local
PK model, and the data commands. Every server/data command exits `6` with
`not implemented in M1`.

## Build

Dependencies: C11, C++17, CMake >= 3.16, OpenSSL, libcurl, nlohmann/json, and a
Rust toolchain (stable).

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

| Command | M1 status |
| --- | --- |
| `version` | Implemented: tool, C core, and Rust printer versions. |
| `init [--reset]` | Implemented: create the encrypted vault. |
| `config path` | Implemented: print the config directory and vault path. |
| `logs show --log-file FILE` | Implemented: decrypt encrypted log records to stdout. |
| `login`, `logout`, `whoami` | Stub: unlock the vault, call the typed `ApiClient` stub, exit `6`. |
| `look`, `add`, `dose`, `lab`, `weight` | Stub: unlock the vault, exit `6`. |
| `export`, `import`, `snapshot` | Stub: unlock the vault, exit `6`. |
| `config get`, `config set`, `logs <other>` | Stub: exit `6`. |

Global flags (accepted before or after the command):

```text
--server URL  --proxy URL  --config-dir DIR  --token-file FILE
--debug  --trace  --json  --no-color  --yes  --timeout SECONDS
--trust-server  --insecure-tls  --transport auto|tls|openssl-v1
--no-cache-cloud-key  --compute local|server
--vault-password-file FILE  --vault-password-stdin  --log-file FILE
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

`--debug`/`--trace` write to stderr by default. With `--log-file FILE`, each
record is:

```text
u32 payload_len | u64 timestamp_ms | 12-byte nonce | AES-256-GCM ciphertext+tag
```

`payload_len` covers the timestamp, nonce, and ciphertext. The first 24 bytes
(length, timestamp, nonce) are the AAD. `hrt-tool logs show --log-file FILE`
decrypts the records to stdout. Plaintext logs are never written.

## Security model

- All local state is encrypted at rest: token, user id, device pass, cached
  cloud key, server/proxy settings, trust pins, and the OpSec acknowledgement.
- Plaintext is written only to stdout. `export` will write an encrypted
  container; plaintext is available only with `--out -` (M2).
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

## Roadmap

- **M2:** port the local PK model, decrypt snapshots, and implement
  `look`/`add`/`dose`/`lab`/`weight`/`export`/`import` against local files and
  test vectors.
- **M3:** add `/api/tool/v1/*` to the website worker on branch
  `Tool-Interface/data`, with tests, wait for green CI, then PR and merge as the
  final step.
- **M4:** wire the live client, SPKI trust prompts, Tor/I2P, encrypted request
  envelopes, and integration tests against local `wrangler dev`.
- Later: calibration (EKF/OU/MIPD), optional server compute, and the GPG F2F
  advanced mode.

## Licence

The repository is MIT.
