# Changelog

## 0.1.1 — 2026-10-21

Documentation and release engineering; no runtime CLI output changes.

### Documentation and release engineering

- The GitHub wiki is the documentation home, versioned in `wiki/` and
  published with `scripts/publish-wiki.sh`.
- The root README is now a guide: Quick Start, Essential Documentation,
  Who Are You, and For Specific Users.
- `hrt-tool`, `hrtcore`, `hrtprint`, and F2F report version `0.1.1` from one
  CMake source; the HTTP User-Agent is derived from the compiled version.
- Release packaging supports linux/amd64, linux/386, windows/amd64, and
  windows/386, with SHA-512 and MD5 checksums; GPG signatures are added by
  the release owner separately.

### Merged `hrt-tool` console and links

- `hrt-f2f` is merged into `hrt-tool`: the Go/OpenPGP implementation is
  built as a `c-archive` and called by C++; the installed binary is still
  `hrt-tool`, and F2F is namespaced `hrt-tool f2f ...` with raw flag
  pass-through and no link/vault inheritance.
- Interactive console: `hrt-tool console` (or bare `hrt-tool` on a TTY),
  `hrt>` base prompt, named links via `add-link`/`commit-server`, and the
  `hrt$link>` prompt with `exit` returning to base. Vault v2 stores each
  link's own encrypted session, language, welcome text, and humour setting.
- `status [all]` prints the current concentration, expected 1m–2d values,
  and recent labs with both unit pairs and the assay; `--json`, `--watch`,
  and `--at` are supported.
- Interactive `add dose` with a `[Yes/No]` summary and real save phases;
  `add dose --debug` shows raw transport diagnostics. Failed mutations
  never print `100% done` and are never retried automatically.
- Proxy: `socks5://` is accepted and rewritten to `socks5h://` (DNS at the
  proxy); `proxy tor|i2p|off|status|test` shortcuts; HTTP(S)/SOCKS4 still
  refused.
- Humorous but diagnostic tunnel/server/timeout messages, a final
  `Oh no...` fallback, `--no-humor`, and `--debug`/`--trace` pure
  diagnostics with no retries.
- Stable exit codes `0`–`14`, `help exit-codes`, and reclassified
  auth/trust/conflict/cancelled/config errors.
- New commands: `console`, `add-link`, `commit-server`, `link`, `status`,
  `proxy`, `doctor`, `completion`, `help`, `f2f`, and `version --json`.
- CMake install rules and a source-tree `Makefile`: `make install` and
  `make update` (cached prefix, no source fetch, no reconfigure), plus
  options for F2F, warnings-as-errors, and ASan/UBSan.
- CI: Go setup, install smoke test, a warnings-as-errors build, and an
  ASan/UBSan job with F2F disabled.

### Original `hrt-tool` M1–M4 work

- New `hrt-tool` terminal client, a separate codebase from `hrt-cold`:
  C11 crypto core, C++17 CLI, and Rust printer.
- Encrypted vault (PBKDF2-SHA256 600k, AES-256-GCM, mode 0600, atomic
  writes) with a master-password entropy gate (>= 40 bits under both the
  website model and the uniform model).
- Encrypted, length-framed log records; plaintext logs are never written.
- Local commands: `look`, `add`, `dose`, `lab`, `weight`, `export`,
  `import`, with PK concentration modelling, cloud-snapshot decryption,
  merge/tombstone semantics, and encrypted `HRTE` exports.
- Live `/api/tool/v1` client: `login`, `logout`, `whoami`,
  `snapshot pull|push|list`, remote `look`, with origin-bound tokens,
  conditional snapshot writes, and conflict retry.
- Sync parity with the website's current v2 payload: `birthYear` and
  `birthYearUpdatedAt` are validated, merged last-write-wins, and included
  in change fingerprints; doses are cleaned with the website's
  `cleanDoseMG` rule on add/edit/import and compared by the cleaned value.
  The encrypted payload keeps the year of birth; `look` does not apply the
  age-based clearance model yet.
- First-contact SPKI SHA-256 pinning stored in the encrypted vault:
  interactive `yes` pins, `--trust-server` persists non-interactively,
  `--yes` trusts for the current run only, and a changed pin is refused
  until re-trusted. The pin is enforced on every TLS handshake, so a key
  swapped between the trust probe and the credential-bearing request
  aborts before the password or token is sent.
- Tor/I2P support: v3 onion validation (56 base32 characters), I2P host
  validation, `config set proxy socks5h://127.0.0.1:9050` /
  `...:4447`, a pre-lookup missing-proxy check that keeps `.onion` names
  away from the local resolver, a `PROXY_DNS` refusal for non-`socks5h`
  proxies, and actionable `TOR_PROXY_MISSING`/`TOR_PROXY_FAILED` errors.
  Environment proxy variables are ignored unless configured explicitly.
- Redirects are refused, so a `307`/`308` cannot replay the login body to
  another origin.
- Added `docs/PRIVACY_ENVIRONMENTS.md` for Qubes, Whonix, and Tails: random
  per-install device keys, disposable-VM device handling, proxy DNS rules,
  the hrt/HTTPS fallback warning, data retention, and the security
  non-guarantees.
- A failed credential login is recorded in the encrypted vault and blocks
  another login attempt for 60 seconds. Server-side rate limiting remains
  the authoritative control.
- I2P destination hashes (52 base32 characters plus `.b32.i2p`) are
  validated alongside `.i2p` address-book names; Tor v3 `.onion` names
  remain 56 base32 characters. A SOCKS5 stub test asserts that a `socks5h`
  request sends the unresolved target name to the proxy, proving target DNS
  stays at the proxy. Documented that `hrt://` requires a self-hosted raw
  TLS listener and is unavailable on edge/serverless platforms.
- Added the `hrt/1` frame codec: a 4-byte big-endian length followed by one
  JSON envelope, capped at 24 MiB so a 16 MiB API body fits after base64
  expansion, with strict length, JSON, and envelope validation. This is the
  wire format used by the self-hosted raw transport.

## 0.1.0 — 2026-10-19 (pre-release)

Initial independent release of Oyama HRT Tracker Tools.

- `hrt-cold` offline OpenPGP cold-backup CLI for the
  `oyama-hrt-complete-backup` envelope.
- `keygen`, `encrypt`, `decrypt`, `inspect`, and `version` commands.
- Sign-then-encrypt with optional signer key; pinned `--verify-key` mode fails
  closed on unsigned input, bad signatures, or a different signer.
- Verified-decrypt local anti-replay high-water mark, with `--allow-older` and
  `--no-replay-check` overrides.
- Strict `.hrtb` container and bundle-envelope validation, bounded parsing,
  atomic no-replace writes, and no plaintext output on failure.
- Static Linux/Windows amd64 and 386 binaries built with `CGO_ENABLED=0`.
- Per-binary SHA-512/MD5 sums and detached GPG signatures in the release.
- Documentation-only C/C++ native core Milestone 0: `configure`, CMake, C ABI
  header, and design/ABI/build/crypto/test documents. No native implementation
  or crypto backend is linked yet.

This is an experimental pre-release. The `.hrtb` format is not stable and the
project has not had an independent cryptographic audit.
