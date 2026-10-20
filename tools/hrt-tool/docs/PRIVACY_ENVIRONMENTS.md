# Qubes, Whonix, and Tails

This page is for people running `hrt-tool` in Qubes, Whonix, Tails, or
another disposable/compartmentalized environment. It explains what works,
what is limited, and what the tool does **not** guarantee.

**Status.** The `hrt://` web-approval pairing transport is implemented in
the CLI and in the self-hosted raw server. The raw server is **off by
default** (`HRT_RAW_ENABLED=0`), so a deployment must enable it; until then
the warned HTTPS password login below applies. Cloudflare/serverless hosting
cannot enable the raw transport.

## Summary

- `hrt-tool` does **not** collect hardware fingerprints, MAC addresses,
  disk serial numbers, hostnames, installed software, or VM type. It uses a
  random device key generated once per installation and stored in the
  encrypted vault.
- Qubes, Whonix, and Tails are supported and treated as ordinary devices.
  No authentication or encryption feature is disabled because a machine is
  a virtual machine.
- A disposable VM that does not keep the vault looks like a **new device**
  every time. The account owner must approve it on the web. Previous
  disposable devices expire automatically and can be revoked at any time.
- The transport follows the configured server URL. Use `hrt://` for the
  self-hosted raw transport. An `https://` server uses the legacy password
  login and **prints a warning on every login**. HTTPS is still encrypted in
  transit and pinned to the server's public key, but it terminates at the
  server or CDN; absolute encryption security is not guaranteed. See
  [HTTPS fallback](#https-fallback). Pass `--require-hrt` to refuse HTTPS
  outright.
- There is no server-sent email or SMS verification code. The code is
  generated locally by the CLI and entered on the web, so there is no
  email/SMS code stream to spam.

## What may be limited

| Area | In a disposable VM |
| --- | --- |
| Device identity | A new random device key per fresh installation; web approval is required each time. |
| Device list | Old disposable entries expire automatically; the account has a device limit and a pending-request limit. |
| Local history | Lost when the VM is destroyed, unless the config directory is on persistent storage. |
| Web history | The account security page keeps the last 12 hours of CLI login requests. |
| Abuse protection | Account-level holds still apply. A VM does not bypass them. |
| Custom transport | Available only on a self-hosted server that supports it; Cloudflare-hosted deployments do not. HTTPS fallback applies. |
| Trusted IPs | User-managed; a Tor exit or shared VPN address is a poor choice for the allowlist because it is shared and changes. |

## Recommended setup

### Qubes

- Run `hrt-tool` in a dedicated AppVM whose private storage persists, and
  keep the config directory (default `~/.config/hrt-tool`) on that volume.
- Do not copy the vault between AppVMs. A copied vault carries the same
  device key; if you must clone it, revoke the old device on the web first.
- A DispVM is fine for a one-off read, but it will be a new device and will
  need approval. Expect the device list to grow and expire.
- Keep the web approval session in a trusted VM, not in the same disposable
  VM as the CLI, if your threat model separates browsing from secrets.

### Whonix

- Run the CLI in the Workstation VM, not in the Gateway.
- Point the proxy at the Gateway's SOCKS listener, for example:

  ```sh
  hrt-tool config set proxy socks5h://10.152.152.10:9050
  ```

  Use the SocksPort your Gateway actually exposes; `socks5h` is required so
  the Gateway resolves the destination name. Do not configure a local DNS
  resolver for the tool.
- The vault lives in the Workstation's persistent home.

### Tails

- Enable Persistent Storage and include the `hrt-tool` config directory if
  you want a stable device key. Without it, every boot is a new device.
- The system Tor SOCKS listener is normally:

  ```sh
  hrt-tool config set proxy socks5h://127.0.0.1:9050
  ```

- If a login request looks unexpected, deny it on the web and revoke the
  device; changing the account password is recommended.

### All environments

- One vault is one device key. Do not copy it to another machine.
- If you restore a backup to a new machine, revoke the old device entry on
  the web.
- A trusted-IP entry is **not** an authentication factor. It only exempts
  that address from the anti-spam hold; web approval, the code, and the
  local password are still required.

## Proxy and DNS

When a proxy is configured, the tool accepts only `socks5h://`. It rejects
`socks5://`, `http://`, and `https://` proxies because they cannot guarantee
that the target host name is resolved by the proxy. Proxy environment
variables (`http_proxy`, `https_proxy`, `ALL_PROXY`) are ignored, and
`no_proxy` cannot bypass an explicitly configured proxy.

The only local lookup that can remain is the proxy's own host name. Use an
IP address (`127.0.0.1`, `10.152.152.10`, or your gateway address) to avoid
even that.

## Security model and non-guarantees

- In the `hrt://` flow the account password is never sent to the server. The
  web session approves the device; the CLI receives a token; the password is
  used locally to derive the cloud-snapshot key.
- The pairing code is generated locally by the CLI and only its SHA-256 hash
  is sent. The code is short-lived and single-use.
- HTTPS fallback still uses the same SPKI pinning, origin-bound token, and
  local cloud-key derivation. What it does **not** protect is the request
  body from the server or a TLS-terminating CDN. That is the reason the
  fallback prints a warning rather than silently downgrading.
- No absolute security guarantee. If the VM, the browser session, the
  account, or the server is compromised, an attacker may approve devices,
  read data, or take over the account. Tor/I2P protect the network path, not
  the endpoint, the account, or the person.
- The anti-abuse system may delay or decline hrt login requests when an
  account is being spammed. It is account-level, does not ban source IPs
  (so shared Tor exits are not collateral damage), and never blocks the
  normal web login. The account security page shows a bombardment count,
  the hold expiry, and a recommendation to change the password.
- No hardware fingerprint is collected. The random device key is used for
  device management and revocation, not for cross-site tracking.

## Data collected and retention

The account security page shows only what is needed to decide whether a
login request is yours:

- account id;
- random device public-key id and the label you chose;
- request time, status, and the last 12 hours of requests;
- the connection IP as seen by the server, stored encrypted, and the country
  code derived from it (no third-party geolocation service);
- anti-abuse counters and the hold expiry;
- trusted-IP entries you add, stored encrypted and managed by you.

Verification-code hashes are kept only until approval or expiry (about five
minutes). Request records are deleted after 12 hours. No hardware
identifiers, VM type, hostname, filesystem path, or installed-software list
is collected.

## HTTPS fallback

The self-hosted `hrt://` transport keeps the pairing and request channel
away from a CDN that terminates TLS. If the deployment has no raw listener,
an `https://` server uses the legacy password login, and the CLI prints on
every login:

> warning: HTTPS terminates at the server (and any CDN in front of it); this
> transport is not end-to-end encrypted. Use an hrt:// server when the
> deployment offers one.

**The HTTPS fallback is the legacy password login.** It sends the account
password to the server inside the TLS session, so the server and any
TLS-terminating CDN can read it; absolute encryption security is not
guaranteed. Web-approval pairing — where the password never leaves the
machine — is implemented only for `hrt://`. The fallback still uses the
pinned server key and the locally derived cloud key, and cloud payloads stay
encrypted in the vault.

If you do not want a fallback, pass `--require-hrt` to `login`; it refuses
any non-`hrt://` server.

Cloudflare-hosted deployments do not support `hrt://`, so they necessarily
use the warned HTTPS password login. Self-hosted deployments can enable the
raw transport and get web-approval pairing.

## If something looks wrong

1. Open **Account → Security** on the web.
2. Check **Recent login requests** for the last 12 hours. Deny anything you
   did not start.
3. Revoke devices you do not recognise.
4. Change the account password, then revoke other web sessions.
5. If the account was being spammed, the page shows the blocked-attempt
   count and when new hrt login attempts will be accepted again.

## See also

- [`../README.md`](../README.md) — build, commands, and the general security
  model.
- The repository `SECURITY.md` — reporting a vulnerability and the threat
  model.
