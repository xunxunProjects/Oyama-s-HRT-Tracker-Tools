# hrt-tool

> The user-facing documentation home is the
> [GitHub wiki](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki).
> This file is the canonical source-tree reference for commands, build
> options, and internal limits.

`hrt-tool` is the terminal client of the independent Oyama HRT Tracker Tools
project. It is a separate codebase from [`hrt-cold`](../hrt-cold/README.md):
different language, build, CI job, and release artifact.

**Status: M4 trust and proxy wiring.** Implemented: the encrypted local
vault, the master-password entropy gate, encrypted debug logs, the CLI
surface, URL/proxy policy, the libcurl transport, first-contact SPKI
trust prompts with pin enforcement, Tor/I2P address policy and proxy
guidance, the live `/api/tool/v1/*` client, the C PK engine (31 reference
scenarios), cloud-snapshot decryption, sync-payload merge/tombstones,
encrypted `HRTE` exports, the complete-backup envelope, the Rust printer,
the local and live `look`/`add`/`dose`/`lab`/`weight` commands, local
`export`/`import`, `login`/`logout`/`whoami`/`snapshot pull|push|list`, and
`config get|set|unset`, including the website's age clearance adjustment.
Still to come: the optional transport envelope and calibration.

## Merged tool, console, and links

`hrt-tool` is now the single installed client. The former `hrt-f2f`
commands live under `hrt-tool f2f ...` and are linked as a Go
`c-archive`; no separate `hrt-f2f` binary is installed. The F2F surface is
a raw pass-through: every token after `f2f` goes to the Go flag parser, so
`hrt-tool f2f login --server ... --token-file ...` keeps its own
semantics. F2F never reads or inherits the account vault's link, token,
proxy, or mode.

Start the interactive console with `hrt-tool console`, or run `hrt-tool`
with no arguments on a terminal. The prompt is `hrt>`. A named link is an
account context with its own encrypted session:

```text
hrt> add-link hrt://hrt.example.org:9443 mtf
hrt> commit-server mtf
hrt$mtf> status all
hrt$mtf> add dose
hrt$mtf> exit
hrt>
```

`exit` in `hrt$name>` returns to the base link and persists it; `Ctrl-D`
keeps the current link. `commit-server -` returns to the base link. The
active link also applies to one-shot commands. `link list|show|rename|remove`
manages links; `link show` prints only redacted session state.

`status [all]` prints the current concentration, the expected values at
1m/5m/10m/30m/1h/2h/4h/8h/12h/24h/2d, and the three most recent blood
tests (E2 in pmol/L and pg/mL, T in ng/dL and nmol/L, plus the assay).
`status all` shows every stored lab; `--at` overrides the offsets;
`--watch SECONDS` re-renders until interrupted; `--json` is stable.

`add dose` with no dose flags starts the interactive interview (drug,
dose, time, route, summary `[Yes/No]`) and prints the record id,
`hrt:YES`, the timestamp/user block, and a real phase progress line.
`--debug` switches to raw transport diagnostics. A failed save never
prints `100% done` and is never retried automatically.

`proxy socks5://HOST:PORT` is accepted and rewritten to `socks5h://`
(DNS at the proxy); HTTP(S)/SOCKS4 proxies are refused. `proxy tor`,
`proxy i2p`, `proxy off`, `proxy status`, and `proxy test` are shortcuts.

Network failures get a short human line plus the technical cause and
error code. `--debug`/`--trace` suppress the human line and all retries;
`--no-humor` only suppresses the human line. Mutations never auto-retry.
Reads retry a tunnel interruption twice.

Exit codes: `0` OK, `1` INTERNAL, `2` USAGE, `3` VAULT, `4` NETWORK,
`5` DATA, `6` NOT_IMPLEMENTED, `7` AUTH, `8` TRUST, `9` PERMISSION,
`10` CONFLICT, `11` CANCELLED, `12` CONFIG, `13` F2F, `14` PLATFORM.
`hrt-tool help exit-codes` prints the table.

## Build

Dependencies: C11, C++17, CMake >= 3.16, OpenSSL >= 3.0, libcurl,
nlohmann/json, a Rust toolchain (stable), and Go 1.23+ for the F2F
c-archive. OpenSSL 3 is required for `EVP_MAC`, `OSSL_PARAM`, and the
HKDF controls.

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
crate has no dependencies, so the offline build is reproducible. The Go F2F
library is built as a `c-archive` with the same CMake build; pass
`-DHRT_TOOL_ENABLE_F2F=OFF` for a build without Go (the `f2f` commands then
return `PLATFORM`/`F2F_DISABLED`).

### Install and update

```sh
# CMake directly
sudo cmake --install tools/hrt-tool/build

# Or the source-tree wrapper (PREFIX defaults to /usr/local)
sudo make -C tools/hrt-tool install
sudo make -C tools/hrt-tool update
```

`make install` configures, builds, and installs `hrt-tool` plus its docs.
`make update` rebuilds and reinstalls using the prefix already cached in
`build/CMakeCache.txt`; it does not fetch source, reconfigure, or touch the
vault. To change the prefix, run `make install PREFIX=/new/prefix` once.

CMake options: `HRT_TOOL_ENABLE_F2F` (ON), `HRT_TOOL_ENABLE_WARNINGS` (ON),
`HRT_TOOL_ENABLE_WERROR` (OFF), `HRT_TOOL_ENABLE_SANITIZERS` (OFF).

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
| `init [--reset]`, `config get\|set\|unset\|path`, `logs show --log-file FILE` | Implemented. |
| `look` | Implemented; without `--snapshot` it pulls and decrypts the newest revision, otherwise it reads the file. |
| `add`, `dose`, `lab`, `weight`, `export`, `import` | Implemented against a local `--snapshot`. |
| `login`, `logout`, `whoami` | Implemented over HTTPS and `hrt://`. `hrt://` uses web-approval pairing and a per-install device certificate; both transports pin the server key on first contact. |
| `snapshot pull`, `snapshot push`, `snapshot list` | Implemented; pull writes an encrypted local snapshot, push merges and retries on `409`. |
| `logs <other>` | Stub; exit `6`. |
| `console` | Interactive `hrt>` / `hrt$link>` console. |
| `add-link URL NAME` | Save a named account context (`hrt://`, `https://`, or policy-limited `http://`). |
| `commit-server NAME` | Select a saved link for this and later commands. |
| `link list\|show\|rename\|remove` | Manage links; `show` redacts secrets. |
| `status [all]` | Current/expected concentrations and recent labs. |
| `proxy tor\|i2p\|off\|status\|test` | Manage or test the SOCKS proxy. |
| `doctor` | Check vault, proxy, server policy, trust pins, versions. |
| `completion bash\|zsh\|fish` | Print a static completion script. |
| `help [exit-codes]` | Command help or the exit-code table. |
| `f2f ...` | F2F keygen/login/register/status/keyring/encrypt/decrypt. |

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
clamped to `0..10000 mg` and cleaned with the website's `cleanDoseMG` rule
(float tails within 0.01% of a whole microgram snap to it, otherwise six
significant figures), weights to `20..400 kg`, invalid lab units default to
`pmol/l`, and records without an id get one. Payloads larger than 16 MiB or
with more than 20,000 records per array are refused. Naked v1 event arrays
are accepted alongside v1 flat objects and v2 `modes` payloads.

The sync merge treats `weight`, `birthYear` and `pkParams` as last-write-wins
scalars with their own `*UpdatedAt` timestamps. `birthYear` is validated as an
integer in `1900..current year`; a present but implausible value is read as
"none" (`null`), matching the website. Doses are compared by their cleaned
value, so a float tail and its tidied copy are not reported as a conflict.
`look` applies the website's age-based clearance adjustment to each dose at
the time it was taken when the payload carries a usable `birthYear`, using
the same 19–75 clamp and E2/T slopes. The year itself is never printed.

Mutating local commands take an advisory lock on `<snapshot>.lock` and hold it
across read-modify-write, so concurrent `add`/`import` processes cannot lose
an update. `look` and `export` do not need the lock and work in a read-only
directory. Exports refuse to overwrite an existing path.

### Remote workflow (live API)

```sh
# One-time login. The token, user id, device pass and (by default) the
# derived cloud key are stored in the encrypted vault.
hrt-tool login --username alice --account-password-file account.txt

# Download the newest revision as an encrypted local snapshot (mode 0600).
# Pull refuses to overwrite an existing path unless --force is given.
hrt-tool snapshot pull --out snapshot.json

# `look` without --snapshot pulls and decrypts the newest revision in memory.
# --all lists every stored lab (the default is the three most recent).
hrt-tool look
hrt-tool look --all --json

# Mutations are live too: pull the newest revision, apply the change, then
# merge and conditional-put with a retry on 409.
hrt-tool add --drug "Estradiol valerate" --time now --measurement 2mg \
  --route injection --mode transfem
hrt-tool dose list
hrt-tool dose edit --id <id> --measurement 3mg
hrt-tool dose delete --id <id>
hrt-tool lab add --time now --value 180 --unit pg/ml --assay lcms
hrt-tool weight set --kg 72

# Or work offline from the downloaded file; every command writes only
# encrypted data. Live mode is skipped whenever --snapshot is present.
hrt-tool look --snapshot snapshot.json --user-id <id> --account-password-file account.txt
hrt-tool add --snapshot snapshot.json --user-id <id> --account-password-file account.txt \
  --drug "Estradiol valerate" --time now --measurement 2mg

# Push a file merge explicitly, or list revisions.
hrt-tool snapshot push --in snapshot.json
hrt-tool snapshot list
hrt-tool logout
```

`snapshot pull` writes only an encrypted cloud bundle; a legacy plaintext
revision is available only with `--out -`. `snapshot push` accepts cloud
bundles, plaintext sync payloads, `HRTE` exports, and complete-backup
envelopes; categories the cloud payload cannot carry are reported.

The server and proxy chosen at login are saved in the vault, so later commands
use them without repeating the flags. A saved token is only sent to that saved
origin: passing a different `--server` to an authenticated command is refused
with `SERVER_MISMATCH`; run `login --server NEW` to switch (which clears the old
origin's token and device pass). `--no-cache-cloud-key` keeps the derived key out
of the vault on that command; pass it to every command that may derive the key,
or the first `pull`/`look` will cache it.

### `hrt://` web-approval login

An `hrt://` login does not send the account password anywhere:

```sh
hrt-tool config set server hrt://hrt.example.org:9443
hrt-tool login --username alice
```

The CLI prints a 128-hex verification code and a web URL, then polls while
the account owner opens **Account → Security**, checks the server-observed
IP/country and device fingerprint, and enters the code. Only the code's
SHA-256 is sent. After approval the CLI asks for the local account password,
which derives the cloud-data key and never leaves the machine. The request
expires after five minutes; a denied or expired request starts the same
hardcoded 60-second login pause as a wrong password.

The first `hrt://` login generates a random device key pair and self-signed
certificate in the encrypted vault. Every later raw request is signed with
that key and carries the certificate; the server verifies the signature
before forwarding it, and the web app binds the issued token to the
certificate fingerprint. The TLS handshake itself does not use a client
certificate. `logout` revokes the token but keeps this per-install identity,
so the next approval reuses the same device instead of consuming a new
device slot; revoke the device itself under **Account → Security** when it
should stop working. Use `--device-name NAME` to choose the label shown in
the web approval page.

`--require-hrt` refuses to fall back to HTTPS during login. Without it, an
HTTPS server still uses the legacy password flow and prints a warning that
HTTPS terminates at the server or its CDN, so the transport is not
end-to-end encrypted.

### `hrt://` transport availability

The self-hosted `hrt://` transport needs a server that can expose a raw
TLS/TCP listener and verify signed device requests. It is **not available** on
edge/serverless platforms that only expose HTTP(S), including Cloudflare
Workers and Pages, Vercel Edge Functions, Netlify Edge Functions, Deno
Deploy, AWS Lambda@Edge, and Fastly Compute@Edge. Those deployments keep the
HTTPS API, which the client warns about. Cloudflare-hosted deployments keep
the existing HTTPS API.

Running an `hrt://` endpoint is an **operator/community responsibility**. The
project ships the protocol, this client, and the reference adapter, but no
public endpoint and no availability guarantee: whoever deploys one maintains
it — TLS and SPKI rotation, patches, monitoring, and Tor/I2P reachability.
Do not assume an official or community instance exists; ask the operator of
the endpoint you use how it is maintained.

Supported hosts for `hrt://` and the HTTPS policy:

- ordinary DNS names and loopback addresses;
- Tor v3 `.onion` names: exactly 56 base32 characters plus `.onion`;
- I2P address-book names ending in `.i2p`;
- I2P destination hashes: exactly 52 base32 characters plus `.b32.i2p`.

`.onion` and `.i2p` names require a `socks5h://` proxy; the name is never
resolved locally. If a proxy is configured, it resolves every target name,
including ordinary DNS names.

### Trust, Tor, and I2P (M4)

Before an authenticated command talks to a server, it fetches
`/api/tool/v1/version` without credentials and decides whether to trust that
origin. On first contact it prints the scheme/host/port, the certificate
subject, issuer, validity, and the SPKI SHA-256, then asks for `yes`.
`--trust-server` persists the decision without prompting; `--yes` accepts the
origin for the current run only and never silently pins a new or changed key.
The pin is stored in the encrypted vault under the origin's `host:port`. Trust
details and the prompt go to stderr, so `--json` output on stdout stays
machine-readable.

A changed SPKI is refused with `TRUST_CHANGED` until `--trust-server` is given
again. Onion and I2P origins always show a stronger warning. Plain `http://`
prints a warning on every run; it is allowed only for loopback, `.onion`, and
`.i2p` hosts. `--insecure-tls` prints a warning on every run as well.

```sh
# Tell the tool how to reach Tor and I2P, once. socks5h keeps DNS at the proxy.
hrt-tool config set proxy socks5h://127.0.0.1:9050   # Tor
hrt-tool config set proxy socks5h://127.0.0.1:4447   # I2P

hrt-tool config get proxy
hrt-tool config unset proxy

# Tor v3 addresses are exactly 56 base32 characters plus .onion.
hrt-tool login --server http://<56-character-v3-address>.onion --username alice \
  --account-password-file account.txt
```

A `.onion` or `.i2p` address with no proxy configured fails with
`TOR_PROXY_MISSING`/`I2P_PROXY_MISSING` and prints the command to run. When a
proxy is configured but the hidden service cannot be resolved, the error names
the proxy and suggests re-running `config set proxy` with the conventional
address (`9050` for Tor, `4447` for I2P).

The `look` numbers include the age adjustment but are otherwise the
uncalibrated baseline: the website's EKF/OU/MIPD calibration is not ported
yet, and `look` prints a warning saying so.

`--time` accepts `now`, `+2h`/`-30m`, `2026y10m19d`, and ISO 8601 with an
optional `Z` or `+HH:MM`/`-HH:MM` offset; an explicit offset names the instant
it denotes regardless of `--utc`. Trailing junk is rejected.

Global flags (accepted before or after the command):

```text
--server URL  --proxy URL  --config-dir DIR  --token-file FILE
--debug  --trace  --json  --no-color  --yes  --force  --probe  --no-humor
--timeout SECONDS  --trust-server  --insecure-tls  --transport auto|tls|openssl-v1
--no-cache-cloud-key  --compute local|server
--vault-password-file FILE  --vault-password-stdin  --log-file FILE
--snapshot FILE  --mode transfem|transmasc  --utc
--user-id ID  --account-password-file FILE  --account-password-stdin
```

## Colour and terminals

`hrt-tool` decides separately for stdout and stderr whether it is talking to
a real terminal. Escape sequences and the welcome banner appear only there:
pipes, files, and `--json` always get plain text. Today the interactive
console's welcome banner and `clear` are the only output that uses this;
all other command output is unchanged.

| Condition | Effect |
|-----------|--------|
| stdout is not a terminal | no escape sequences; one-line console welcome |
| `TERM` unset, empty, or `dumb` | no escape sequences (the banner is still drawn, in plain ASCII) |
| `NO_COLOR` set to any non-empty value, including `0` | no colour; an empty value is ignored |
| `--no-color` | plain mode: no escape sequences at all, so `clear` does not wipe the screen either |
| `TERM=linux` (Linux kernel console) | colour, but no faint text |
| `--json` | no banner |

Colours are the terminal's own basic ANSI colours (cyan accent, bold title,
faint secondary text), never fixed RGB values, so your theme decides the
shades. The width comes from the terminal, then `COLUMNS`, then 80; below 52
columns the banner shrinks to two lines. The banner is plain ASCII.

A custom welcome text replaces the banner and is printed exactly as given,
also on non-terminals: `hrt-tool config set welcome TEXT`. `hrt-tool config
unset welcome` brings the banner back.

On Windows, real consoles (cmd.exe, PowerShell, Windows Terminal) are switched
to virtual-terminal mode and a UTF-8 output code page for the life of the
process (restored on exit); an unset `TERM` counts as `xterm` there. MSYS2,
mintty, and Git Bash connect stdout through a pipe, so they count as
non-terminals and get plain output. This path is compile-checked for both
MinGW targets but has not been run on a real Windows console yet.

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

`--debug`/`--trace` enable transport and TLS diagnostics on stderr; request
and response headers are redacted before printing. Diagnostics are never
written to disk in plaintext. With `--log-file FILE`, each record is:

```text
u32 payload_len | u64 timestamp_ms | 12-byte nonce | AES-256-GCM ciphertext+tag
```

`payload_len` covers the timestamp, nonce, and ciphertext. The first 24 bytes
(length, timestamp, nonce) are the AAD. `hrt-tool logs show --log-file FILE`
decrypts the records to stdout. Plaintext logs are never written.

## Security model

People running the tool in Qubes, Whonix, Tails, or another disposable VM
should read [`docs/PRIVACY_ENVIRONMENTS.md`](docs/PRIVACY_ENVIRONMENTS.md):
it covers persistent vaults, per-install device keys, proxy DNS, the
`hrt://` transport, the HTTPS fallback warning, and the non-guarantees.

- All local state is encrypted at rest: token, user id, device pass, cached
  cloud key, server/proxy settings, trust pins, and the OpSec acknowledgement.
- Plaintext is written only to stdout. `export --out FILE` writes an encrypted
  `HRTE` container; plaintext is available only with `--out -`. `import`
  accepts encrypted `HRTE` or plaintext input.
- `export --out FILE` refuses to replace an existing file, and all local reads
  are capped at 16 MiB after a regular-file check, so a FIFO or a growing file
  cannot exhaust memory.
- The stored JWT is bound to the origin that issued it; an authenticated
  command with a different `--server` is refused instead of sending the token
  elsewhere. `logout` always clears the local token, cached cloud key and
  device pass, even when the revocation request fails.
- Login sends the account password to the server, exactly as the website does.
  The cloud key is derived from that password and the user id, so a
  compromised worker or request log can derive the key. The "server cannot read
  health data" property holds for an honest-but-curious server that follows the
  protocol; it does not survive a compromised endpoint. Do not reuse a password
  from another service. A failed credential attempt stores a timestamp in the
  encrypted vault and refuses another login for 60 seconds; the server still
  enforces its own rate limits, because a modified client can skip the local
  pause.
- The tool does not call `system`, `popen`, `exec`, `dlopen`, or a shell, and
  never executes or evaluates server-provided content. Server strings are data.
- HTTPS is required for ordinary remote servers. Plain `http://` is accepted
  only for loopback, `.onion`, and `.i2p` hosts, requires an explicit trust
  decision (or an already-pinned origin), and prints a warning on every run.
- Trust pins the SPKI SHA-256, not the leaf certificate, so certificate
  renewals do not break the client. A first contact requires interactive
  `yes` or `--trust-server`; a changed pin is refused until `--trust-server`
  is given.
- The pin is enforced during every TLS handshake through libcurl's
  `CURLOPT_PINNEDPUBLICKEY`, not only on the trust probe. A key swapped
  between the probe and the credential-bearing request aborts the handshake
  before the request body (password or token) is sent. The captured
  certificate is also checked again after the transfer as defence in depth.
- The trust probe is an unauthenticated `GET /api/tool/v1/version`; it must
  answer `200` before the origin is considered. Credentials are read or sent
  only after the trust decision.
- Redirects are not followed. A `3xx` is a typed error, so a `307`/`308`
  cannot re-send the login body (the account password) to another host.
- `logs show` requires `--log-file FILE`; no default log location is written.
- Windows relies on NTFS ACLs rather than Unix mode bits; the Windows build
  and ACL verification are still pending, and the Windows vault write is not
  yet crash-durable (`fsync` is POSIX-only here).
- Tor/I2P are reached through `config set proxy socks5h://127.0.0.1:9050`
  (Tor) or `socks5h://127.0.0.1:4447` (I2P). `socks5h` keeps DNS at the
  proxy. Only `socks5h://` is accepted as a proxy; `socks5://`, `http://`,
  and `https://` proxies are refused with `PROXY_URL`, because they cannot
  guarantee that the destination name is resolved by the proxy. A missing
  proxy produces `TOR_PROXY_MISSING`/`I2P_PROXY_MISSING` with the exact
  configuration command to run; the check happens before any DNS lookup, so
  the hidden-service name never reaches the local resolver. A non-`socks5h`
  proxy saved by an older build is refused with `PROXY_DNS` for hidden
  services. A failed resolution produces
  `TOR_PROXY_FAILED`/`I2P_PROXY_FAILED` with the configured proxy and the
  conventional address. Only Tor v3 onion names (56 base32 characters) are
  accepted.
- Routing is explicit: the tool ignores `http_proxy`, `https_proxy`, and
  `ALL_PROXY` from the environment, and `no_proxy` cannot bypass an
  explicitly configured proxy. A `.onion` or `.i2p` address without a
  configured proxy fails instead of being sent to an unexpected proxy or a
  local resolver.
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
- **M3 (implemented):** `/api/tool/v1/*` in the website worker (PRs #133/#134)
  and the live client: `login`, `logout`, `whoami`, `snapshot pull|push|list`.
- **M4 (implemented):** first-contact SPKI trust prompts, pin enforcement,
  `config get|set|unset`, Tor/I2P v3 address policy, proxy configuration and
  failure guidance, live `look`/`add`/`dose`/`lab`/`weight`, and integration
  tests against a loopback API (the live client was exercised against local
  `wrangler dev`). The optional encrypted request envelope remains.
- Later: calibration (EKF/OU/MIPD) and optional server compute. GPG/F2F now
  lives in the separate `hrt-f2f` tool.

## Licence

The repository is MIT.
