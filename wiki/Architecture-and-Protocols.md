# Architecture and Protocols

## Components

```text
            +-----------------------------+
            |          hrt-tool           |
            |  C++ CLI + C core + Rust    |
            |  printer + Go F2F c-archive |
            +--------------+--------------+
                           |
          +----------------+----------------+
          |                                 |
   HTTPS /api/tool/v1                raw hrt/1 (TLS)
          |                                 |
   website Worker / API              self-hosted raw server
```

`hrt-cold` is separate and offline; it never talks to the server.

## Vault

- Path: `<config-dir>/vault.hrtv`, mode `0600`.
- Key derivation: PBKDF2-SHA256, 600,000 iterations.
- Payload: AES-256-GCM, versioned JSON (payload v2).
- Contents: account session, device key/cert, cloud key, proxy/server policy,
  trust pins, language/welcome/humor, active link, and named links. Each link
  has its own encrypted session and secrets.
- Writes are atomic; the old file is replaced only after the new one is
  complete. A vault lock serializes one-shot writers; the console holds the
  lock for its lifetime.

## HTTPS API

- Public: `GET /version`, `POST /login`, `POST /compute`.
- Authenticated: `me`, `logout`, snapshot pull/push/list.
- Tokens are origin-bound. Snapshot writes use `If-Match` and retry on `409`.
- Errors use a typed JSON body; auth failures map to exit `7`, trust failures
  to exit `8`.

See [`docs/tool-interface/TOOL_API.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/blob/main/docs/tool-interface/TOOL_API.md).

## Raw hrt/1

- TLS with ALPN and a per-install device certificate.
- Length-prefixed frames with a canonical signature string.
- Pairing: the CLI shows a 128-hex code; the web session approves the device;
  the CLI receives a token. The account password is never sent.
- The raw server is opt-in (`HRT_RAW_ENABLED=0` by default).
- Every request is checked against an operation allowlist.

See [`docs/hrt-protocol/DEV_GUIDE.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/blob/main/docs/hrt-protocol/DEV_GUIDE.md).

## Data and sync

- Snapshots are encrypted complete-backup payloads.
- Merge uses last-write-wins per record plus tombstones; bounds are applied
  before use.
- The PK engine is validated against the website's reference scenarios; the
  status view reuses the same model.

## F2F

- Keys are member-owned; only public keys and fingerprints are uploaded.
- The cohort keyring is downloaded and used locally.
- Encryption is authenticated; signing is optional.
- The F2F implementation is a Go c-archive linked into `hrt-tool f2f ...`.

## Canonical sources

- `docs/tool-interface/TOOL_API.md`
- `docs/hrt-protocol/DEV_GUIDE.md`
- `docs/backup-schema-audit/TOOLS_INTERFACE.md`
- `docs/native/ABI.md`
