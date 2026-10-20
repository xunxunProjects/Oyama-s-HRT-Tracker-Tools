# hrt pairing and self-hosted transport — design

**Status:** draft, 2026-10-20. Cloudflare-hosted support is explicitly out
of scope; the feature is default-off and must advertise that it is
unavailable there.

**Related:** [`2026-10-19-hrt-tool-design.md`](2026-10-19-hrt-tool-design.md),
[`../../tools/hrt-tool/docs/PRIVACY_ENVIRONMENTS.md`](../../tools/hrt-tool/docs/PRIVACY_ENVIRONMENTS.md),
[`../tool-interface/TOOL_API.md`](../../tool-interface/TOOL_API.md).

## 1. Goals

1. A login that never sends the account password to the server.
2. A web-approved device binding that shows the account owner the source IP
   and country before approving it.
3. A client-generated high-entropy code that proves the approving person is
   holding the CLI that requested access.
4. A self-hosted `hrt://` transport that is preferred over HTTPS, with a
   loud, explicit HTTPS fallback when it is unavailable.
5. Account-level anti-abuse that survives device-key rotation and virtual
   machines, without banning source IPs or collecting hardware identifiers.
6. Minimal data collection with short, documented retention.
7. A web Account → Security area that adds visibility without touching the
   existing web login/session code.

## 2. Non-goals

- A hand-rolled cryptographic handshake or cipher. The transport uses TLS
  1.3 and device certificates; application data stays JSON over the existing
  `/api/tool/v1` routes.
- `hrt://` on Cloudflare Workers. Workers provide HTTP(S) only; a raw
  transport cannot run there.
- Global IP bans or a global device-fingerprint ban list. Both are
  unreliable and collide with Tor, VPNs, and privacy users.
- Hardware fingerprints, MAC addresses, serial numbers, hostnames, VM
  detection, or installed-software enumeration.
- Email or SMS verification codes. The code is CLI-generated and displayed
  locally, so there is no server-sent code stream to spam.
- Absolute security guarantees. The project provides a threat model and
  engineering commitments, not a warranty.

## 3. Deployment and capability model

`GET /api/tool/v1/version` gains a `pairing` capability object. Exact JSON
keys are fixed in the server contract; the client treats an absent object as
unsupported.

- **Cloudflare-hosted:** `pairing.supported: false`,
  `transport.hrt: false`, reason `serverless`. The existing HTTPS tool API
  and web login remain unchanged. The client refuses `hrt://` and may use
  the warned HTTPS fallback. The same applies to other edge/serverless
  runtimes that expose only HTTP(S) or cannot accept inbound raw TLS/client
  certificates: Vercel Edge Functions, Netlify Edge Functions, Deno Deploy,
  AWS Lambda@Edge, and Fastly Compute@Edge, plus static hosting. The README
  and every PR that ships this feature must state the limitation.
- **Self-hosted:** `pairing.supported: true` only when the operator enables
  the feature; `transport.hrt: true` only when the TLS/mTLS listener is
  configured. Default off. Enabling requires an explicit configuration
  value and server key material; it never activates automatically.
- The capability response is served over the pinned HTTPS origin, so a
  network attacker cannot forge it without breaking the pin. A capability
  failure is not a reason to silently downgrade; it is an error the user
  can choose to fall back from.

## 4. The `hrt://` scheme

- Syntax: `hrt://host[:port][/]`. No userinfo, query, fragment, or path
  beyond `/`. The host is a real DNS name, loopback, Tor v3 `.onion`
  (exactly 56 base32 characters), an I2P address-book name ending in
  `.i2p`, or an I2P destination hash (exactly 52 base32 characters plus
  `.b32.i2p`). The scheme does not introduce a new naming system.
- Special names are classified before any connection: `.onion` and `.i2p`
  require `socks5h://` and are never resolved locally. `hrt://` itself uses
  TLS, so private/LAN names are allowed when the pinned server certificate
  matches; the `https://` HTTP policy does not apply to the raw transport.
- `hrt://` is a strict profile:
  - clearnet: TLS 1.3, server SPKI pinned, client device certificate.
  - `.onion`/`.i2p`: the configured `socks5h://` proxy is mandatory and the
    name is never resolved locally; the server still presents a pinned TLS
    identity or the transport's device-authenticated channel.
- No operating-system URI handler is registered. The scheme is only parsed
  by `hrt-tool`; a web page cannot launch the CLI.
- The canonical origin for token binding is stored separately from the
  presentation scheme, so switching between `hrt://` and `https://` for the
  same server does not create two credential scopes.
- `--require-hrt` (or the equivalent config value) disables the fallback.
  Without it, a failed or unsupported `hrt://` connection may fall back to
  HTTPS after printing the warning in §8.

## 5. Pairing state machine

States: `initiated` → `web_pending` → `approved` → `token_issued`;
`denied`, `expired`, and `held` are terminal for that request.

### 5.1 Start (CLI, unauthenticated)

`POST /hrt/pair/start`

```json
{
  "code_hash": "<hex sha256 of the 128-hex code>",
  "device_public_key": "<public key>",
  "device_name": "<user-visible label, bounded>",
  "client_version": "hrt-tool/0.1.0"
}
```

The server records the connection source IP (from the platform's trusted
header, never a client-supplied value) and the country code. It returns:

```json
{
  "request_id": "<opaque id>",
  "poll_secret": "<high-entropy secret, separate from the code>",
  "expires_at": 1760000000,
  "verification_url": "https://server/account/security/pair/<id>"
}
```

The code itself is never sent. Only its SHA-256 hash is stored. The code is
generated by the CLI with 64 random bytes and printed as 128 lowercase hex
characters (`openssl rand -hex 128`). It is held in memory only.

### 5.2 Web approval

The account owner, already signed in, opens Account → Security and sees the
pending request: device name, public-key fingerprint, server-observed IP,
country, and time. The page requires:

1. an explicit **Approve** action (CSRF-protected);
2. the 128-hex code displayed by the CLI, entered on the page;
3. a constant-time comparison of the code hash by the server.

Approval does not require the account password. Adding a **trusted IP** in
the same flow does require one re-entry of the web account password,
verified with the existing password machinery; that re-authentication does
not create a new session or change the web login code.

The web page may offer **Deny**. Denial and expiry invalidate the request.

### 5.3 Poll and token

`POST /hrt/pair/poll` with `request_id` and `poll_secret` returns
`pending`, `approved`, `denied`, `expired`, or `held`. On approval it
returns a token, the user id, and the username. The token is bound to the
origin and the device public key. `hrt://` requests are additionally bound
to the device certificate by the transport. Over the HTTPS fallback the
token remains origin-bound; request proof-of-possession can be added later
without changing the pairing flow.

The CLI then prompts for the account password. The password is used only
locally to derive the cloud-snapshot key; it is never sent. A wrong
password shows as decryption failure, not as a failed login.

### 5.4 Codes

- 128 hex characters, generated with `RAND_bytes`; never accepted from
  `argv`, environment variables, or a log.
- Stored only as SHA-256; TTL about five minutes; single-use; constant-time
  comparison; pending-request limits and rate limits.
- No email/SMS delivery.

## 6. Anti-abuse

The goal is to protect an account from a login-spam flood and to keep the
service usable, not to identify attackers.

- **Account-level hold:** when an account shows abusive hrt login-request
  volume across configurable burst and slow windows, set a one-hour hold.
- **Black hole:** during the hold, every hrt login request for that account
  is accepted and performs no work: no pending request, no code, no token,
  no notification. Responses are indistinguishable from an ordinary
  pending/failed state. No fake tokens or fake success are returned.
- **Web unaffected:** the existing web login and sessions keep working.
  Existing hrt tokens stay valid until expiry or revocation, so the hold
  does not disconnect the user's other devices.
- **No IP ban:** source IPs are not blocked globally. Per-IP counters are
  keyed by an HMAC of the authoritative source IP and expire quickly.
- **No device-key ban list:** a device key can be revoked for its account,
  but there is no global list; a VM or a fresh install gets a new key, and
  the account hold still applies.
- **Trusted IPs:** user-managed entries exempt that address from the hold
  and from the abuse counters. They are not an authentication factor: web
  approval, the code, and the local password are still required. Adding or
  removing an entry requires an authenticated web session and a password
  re-entry; entries are encrypted at rest, displayed masked, capped in
  number, and can be revoked.
- **Slow abuse:** the same account hold covers low-and-slow request rates;
  per-account pending/device caps and per-IP counters bound the cost. A
  proof-of-work or web CAPTCHA may be added as an optional speed bump only
  if operational data shows it is needed; it is not a default and is not
  VM-specific.
- **No VM detection.** Qubes, Whonix, Tails, and ordinary machines are
  treated identically. Disposable devices are handled by expiry and caps,
  not by weakening authentication.
- Exact thresholds and window sizes live in server configuration and are
  not part of the public user documentation. They are not a security
  boundary: the code and behavior must remain safe if an attacker learns
  them.

### 6.1 Authoritative source IP

The IP shown to the account owner and used for trusted-IP matching is the
address the server observes:

- Cloudflare: `CF-Connecting-IP` from the platform, never a client-supplied
  `X-Forwarded-For`.
- Self-hosted: the socket peer unless a trusted reverse-proxy chain is
  explicitly configured; untrusted forwarding headers are ignored.

A spoofable IP header would let an attacker claim a trusted IP and bypass
the hold, so this is a required test case.

## 7. Data model and retention

Proposed tables (names may change in implementation):

| Table | Fields | Retention |
| --- | --- | --- |
| `hrt_pair_requests` | id, account id, device key id, code hash, encrypted source IP, country code, status, timestamps | 12 hours; code hash deleted on approval/expiry |
| `hrt_devices` | account id, public-key id, label, created, last seen, expires, revoked | until revoked or unused-expiry; default 90 days, disposable shorter |
| `hrt_holds` | account id, hold until, blocked count, first/last blocked | 1 hour hold; aggregate count at most 24 hours |
| `hrt_trusted_ips` | account id, encrypted address/prefix, label, created, last used | until removed or 90 days |
| rate-limit counters | HMAC(source IP), window, count | window length only |

Not collected: hardware identifiers, VM type, hostname, filesystem paths,
installed software, browsing data, precise location, email/SMS codes, or
third-party analytics. Country codes come from Cloudflare's request
metadata or a local GeoIP database; the source IP is never sent to a
third-party geolocation API.

The account security page exposes the last 12 hours of requests, the
bombardment count, the hold expiry, and a password-change recommendation.
Request records never expose the code or its hash.

## 8. Transport and HTTPS fallback

### 8.1 Preferred transport

Self-hosted: TLS 1.3 with a client device certificate (mTLS). The client
generates a random device key pair at install, stores the private key in
the encrypted vault, and obtains the server's pinned identity at pairing.
No raw custom protocol is introduced in this phase.

### 8.2 Fallback

If `hrt://` is unsupported or the transport cannot be used, the CLI may
continue over HTTPS. Every run prints:

> hrt transport unavailable; using HTTPS. HTTPS protects the link to the
> server, but the server or its TLS-terminating CDN can read request bodies.
> Absolute encryption security is not guaranteed.

The fallback:

- still uses SPKI pinning and the origin-bound approved token;
- still never sends the account password;
- still uses the locally derived cloud key for snapshot data;
- must not silently ignore a failed capability or failed pin.

`--require-hrt` disables the fallback. Cloudflare-hosted deployments are
expected to use the fallback (or plain HTTPS) and must say so in their
capability response and user documentation.

## 9. Web Account → Security

All new UI lives under Account → Security. Existing login, session, and
password handlers are not modified.

- **Overview:** bombardment count, hold expiry, password advice, device
  link.
- **Recent login requests:** last 12 hours; time, device name, key
  fingerprint, IP, country, status. Deny pending requests.
- **Trusted IPs:** add (password re-entry), remove, masked display, last
  used.
- **Devices:** label, created, last seen, expires, revoke.
- **Privacy environments:** link to the Qubes/Whonix/Tails document.

All new endpoints require the existing authenticated web session, CSRF
protection, and rate limits. Read-only display endpoints never return the
code or its hash.

## 10. Client changes

- Vault additions (encrypted): device key pair, device key id, pairing
  state, trusted server pin, last failed login timestamp.
- `login` gains the pairing flow when the server advertises it; the legacy
  password login remains for HTTPS-only servers.
- `hrt://` parsing, capability refusal, fallback warning, and
  `--require-hrt`.
- The local 60-second login-failure pause is already implemented; the
  pairing flow uses the same rule for its credential/token failures.
- Proxy policy is already `socks5h` only; all hrt commands must use the
  same `HttpOptions` and never resolve target names locally.
- No hardware fingerprint collection; the device key is random and
  revocable.

## 11. Test plan

- Pairing: code hash only, TTL, single use, replay, wrong code, denial,
  expiry, poll secret separation, token binding.
- Web approval: CSRF, session required, password re-entry for trusted IPs,
  no code/hash exposure.
- Anti-abuse: account hold, uniform responses, no state change, no token,
  no notification, old tokens unaffected, web login unaffected, pending
  cap, device cap, slow-window trigger, hold expiry.
- Trusted IP: password re-auth, encrypted storage, masked display,
  matching only the authoritative IP, spoofed `X-Forwarded-For` rejected.
- VM: rotating device keys does not bypass the account hold; disposable
  devices expire; no VM detection or hardware identifiers.
- Transport: mTLS success, pin mismatch, capability unsupported, fallback
  warning, `--require-hrt`, no password on the wire, proxy DNS.
- Data: retention cleanup, no plaintext IP/log/code, no third-party calls.
- Web auth regression: existing login/session tests unchanged.

## 12. Phases

1. **Client groundwork (partly done):** socks5h-only proxy, 60-second
   login pause, privacy-environment document, fallback warning copy.
2. **Pairing protocol:** server endpoints + CLI state machine + loopback
   tests.
3. **Self-hosted transport:** TLS 1.3/mTLS listener, device certificates,
   capability advertisement, default off.
4. **Web:** Account → Security, approval page, 12-hour list, trusted IPs.
5. **Anti-abuse:** hold, counters, blackhole, device caps, cleanup.
6. **Hardening/PR:** negative tests, threat model, PR notes for the
   Cloudflare limitation and default-off behavior.

## 13. Open questions

- Whether pairing should also be offered on the Cloudflare-hosted site over
  HTTPS in a later phase.
- Exact retention for device entries and trusted IPs (90 days is a
  starting point).
- Whether the HTTPS fallback should require device proof-of-possession now
  or later.
- Local GeoIP database and licensing for self-hosted country display.

## 14. Selected raw-server architecture

The operator selected a standalone raw protocol server rather than an
application-layer envelope over HTTPS. The selected shape is a **TLS/mTLS
protocol adapter** in front of the existing self-hosted web application:

- **One account system.** The web application remains the source of truth
  for accounts, pairing requests, device registrations, holds, and trusted
  IPs. The raw server does not create a second account database and does
  not reimplement password auth. This keeps the original web auth code
  untouched and avoids a second security boundary to audit.
- **Raw server responsibilities.** A standalone Node.js/TypeScript process
  owns the TLS 1.3 listener, the server certificate, mTLS certificate
  fingerprint checks, the framed protocol, connection limits, and the
  loopback call to the web app. It stores no account data and has no device
  CA; the web app owns the device registry.
- **Wire protocol.** ALPN `hrt/1`; TLS 1.3 with a pinned server key; client
  certificates required after pairing. Frames are a 4-byte big-endian
  length followed by a JSON object with a request id, an operation
  (`pair.start`, `pair.poll`, `request`, `ping`), and a bounded body. `request`
  is allowlisted to `/api/tool/v1/*` and a small header set; the raw server
  is not a general forward proxy. Frames are capped (24 MiB, enough for a
  16 MiB JSON body after base64 expansion) with per-connection timeouts,
  bounded connections, and request-id idempotency.
- **Pairing path.** `pair.start` and `pair.poll` are the only operations
  allowed without a client certificate. The CLI generates a self-signed
  device certificate and sends it in `pair.start`; the web app records its
  SHA-256 fingerprint on approval. The raw server forwards the pair frames
  to the web app's internal `/api/tool/v1/hrt/*` endpoints over loopback,
  authenticated by a service token in a 0600 file, and returns the token.
  Subsequent frames require a client certificate whose fingerprint the web
  app authorizes.
- **Web approval.** The existing web session calls additive
  Account → Security endpoints in the web app; the web app owns the data.
  The raw server never serves the browser session.
- **Data path.** The raw server translates allowlisted frames to HTTP
  requests to the web app at loopback, forwarding `Authorization`,
  `If-Match`, and the verified device key. The web app validates the token
  and device binding; the raw server does not interpret health data.
- **Deployment.** The self-hosted Docker image runs the web app and the raw
  server as separate processes on the same volume. The raw server listens
  on a distinct port (for example 9443); `HRT_RAW_ENABLED=0` is the default,
  and the web capability advertises `hrt` only when the operator enables
  it. Cloudflare deployments cannot run the raw server and advertise it as
  unsupported.
- **Client.** `hrt-tool` uses an OpenSSL-based raw client (TLS 1.3, SPKI
  pin, optional client certificate) with a SOCKS5h connector for Tor/I2P.
  It falls back to HTTPS with the warning in §8.2 when the raw transport is
  unavailable or the operator requires it.

This shape gives the operator a raw, self-controlled encrypted channel
without duplicating the account/auth stack. The open question is whether
the operator wants the raw server to remain an adapter (recommended) or to
become a fully independent account system; the latter would duplicate
password/auth logic and is not part of this design.
