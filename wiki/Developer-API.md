# Developer API and raw transport

The website and `hrt-tool` share one versioned API:
`/api/tool/v1` on the same Worker. Snapshot payloads are opaque JSON: the
server never decrypts them, derives the cloud key, or computes
concentrations. The canonical contract is
[`docs/tool-interface/TOOL_API.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/blob/main/docs/tool-interface/TOOL_API.md);
this page is the working guide.

## Base URL and authentication

- Base: `https://<server>/api/tool/v1`
- Auth: `Authorization: Bearer <token>` from `/api/tool/v1/login`
  (a normal 7-day session token; passkey challenge tokens are rejected).
- Errors: JSON `{ "code": "...", "message": "..." }`; `401` carries
  `X-Session-Invalid: 1` when the session is missing, expired, revoked, or
  idle-timed-out.
- CORS is `*`; the CLI uses no cookies.

## Public routes

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/version` | `apiVersion`, `minToolVersion`, transports, server time. |
| POST | `/login` | The website login body plus optional `totp_code`, `backup_code`, `device_pass`; returns `token`, `user`, `devicePass`. |
| POST | `/compute` | Reserved; always `501 NOT_IMPLEMENTED`. |

## Authenticated routes

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/me` | Current user. |
| POST | `/logout` | Delete the session named by `sid`. |
| GET | `/snapshot` | Newest retained revision, or `404 NO_SNAPSHOT`. |
| GET | `/snapshots` | Revision metadata, newest first (at most five retained). |
| GET | `/snapshot/:id` | One revision; another account's id is `404`, never `403`. |
| PUT | `/snapshot` | Store `{ "data": <opaque JSON> }` as a new revision. |

`PUT /snapshot` accepts `If-Match: <baseRevisionId>`, a quoted or `W/`-prefixed
id, or `If-Match: *` (requires an existing revision). If the newest revision
changed, the write is rejected with `409 CONFLICT` and a `current` object. The
CLI reads, merges, writes conditionally, and refetches on conflict. Without the
header the write is last-writer-wins.

Limits: 2 MiB request cap, `content-write:<userId>` 20/min, five retained
revisions; the stream is cancelled as soon as it passes the cap.

## Quick calls

```sh
BASE=https://hrt.example.org

curl -s "$BASE/api/tool/v1/version"

curl -s -X POST "$BASE/api/tool/v1/login" \
  -H 'Content-Type: application/json' \
  -d '{"username":"alice","password":"..."}'

TOKEN=...
curl -s "$BASE/api/tool/v1/me" -H "Authorization: Bearer $TOKEN"

curl -s "$BASE/api/tool/v1/snapshot" -H "Authorization: Bearer $TOKEN"

curl -s -X PUT "$BASE/api/tool/v1/snapshot" \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -H 'If-Match: <base-revision-id>' \
  -d '{"data":{"cloud":1,"iv":"...","data":"..."}}'
```

`data` is the E2E-encrypted `{cloud:1, iv, data}` bundle, a legacy plaintext
payload, or any other JSON value. The server stores it as-is.

## Raw `hrt/1` transport

The optional `hrt://` listener is a separate TLS 1.3 service
(`hrt-server/`). It advertises ALPN `hrt/1`, carries 4-byte big-endian length
prefixes followed by JSON frames capped at 24 MiB, and exposes only
`pair.start`, `pair.poll`, `ping`, and an allowlisted `/api/tool/v1/*`
surface. Every request is signed by the per-install device key and carries
the device certificate; the server verifies the signature and binds the
issued token to the certificate fingerprint. The TLS handshake itself does
not use client certificates.

`hrt://` is **not available** on HTTP-only edge platforms (Cloudflare
Workers, Vercel/Netlify/Deno edge, Lambda@Edge, Fastly Compute). Those
deployments keep the HTTPS API and its server-terminated-TLS warning.

## Client-side rules worth copying

- Store tokens per origin and refuse an explicit different `--server`
  (`SERVER_MISMATCH`).
- Do not follow HTTP redirects when sending a bearer token.
- Pin the server certificate/SPKI on first contact; refuse a changed pin
  until the user re-trusts.
- Keep password, token, snapshot, and log files at mode `0600`.
- Treat imported routes, units, weights, and record counts as untrusted and
  sanitise them (the CLI does).

## Tests and canonical sources

- Tool API contract: [`docs/tool-interface/TOOL_API.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/blob/main/docs/tool-interface/TOOL_API.md)
- Architecture: [Architecture and Protocols](Architecture-and-Protocols)
- Build: [Build and Install](Build-and-Install); cross-builds: [Cross Compilation](Cross-Compilation)
- Tests: `ctest --test-dir tools/hrt-tool/build --output-on-failure`, plus the
  Rust and Go suites described in the [Development Guide](Development-Guide).
- Web/Worker API: [Developers](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker/wiki/Developers) in the main wiki.
