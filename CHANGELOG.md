# Changelog

## Unreleased — `hrt-tool`

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
