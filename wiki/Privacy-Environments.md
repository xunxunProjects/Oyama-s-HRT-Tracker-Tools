# Privacy Environments

Guidance for Qubes, Whonix, Tails, and other compartmentalized or disposable
environments.

## What the tool does not collect

`hrt-tool` does not collect hardware fingerprints, MAC addresses, disk serial
numbers, hostnames, installed software, or VM type. It uses a random device key
generated once per installation and stored in the encrypted vault.

Qubes, Whonix, and Tails are treated as ordinary devices. No authentication or
encryption feature is disabled because a machine is a VM.

## Disposable VMs

A VM that does not keep the vault looks like a new device every time. The
account owner must approve it on the web. Old disposable devices expire
automatically and can be revoked at any time.

| Area | In a disposable VM |
| --- | --- |
| Device identity | New random device key per fresh installation; web approval required. |
| Device list | Old entries expire; the account has device and pending-request limits. |
| Local history | Lost with the VM unless the config directory is persistent. |
| Web history | The account keeps the last 12 hours of CLI login requests. |
| Custom transport | Only on a self-hosted raw server; Cloudflare deployments use HTTPS. |

## Recommended setup

### Qubes

- Run `hrt-tool` in a dedicated AppVM with persistent private storage.
- Keep the config directory (`~/.config/hrt-tool`) on that volume.
- Do not copy the vault between AppVMs; a copied vault carries the same device
  key. Revoke the old device on the web first.
- Keep the web approval session in a trusted VM if your threat model separates
  browsing from secrets.

### Whonix

- Run the CLI in the Workstation, not the Gateway.
- Point the proxy at the Gateway SOCKS listener:

```sh
hrt-tool config set proxy socks5h://10.152.152.10:9050
```

- Use the SocksPort your Gateway exposes. The proxy must resolve the target
  name; do not configure local DNS for the tool.

### Tails

- Enable Persistent Storage and include the `hrt-tool` config directory for a
  stable device key. Without it, every boot is a new device.
- The system Tor SOCKS listener is normally:

```sh
hrt-tool config set proxy socks5h://127.0.0.1:9050
```

- If a login request looks unexpected, deny it on the web, revoke the device,
  and consider changing the account password.

## Proxy and DNS

- `socks5://` and `socks5h://` are accepted; `socks5://` is rewritten to
  `socks5h://`, so the proxy resolves every name.
- `http://` and `https://` proxies are refused.
- Proxy environment variables (`http_proxy`, `https_proxy`, `ALL_PROXY`) are
  ignored, and `no_proxy` cannot bypass an explicitly configured proxy.
- The only local lookup that can remain is the proxy's own host name. Use an
  IP address (`127.0.0.1`, `10.152.152.10`, or your gateway address) to avoid
  even that.

## HTTPS fallback

An `https://` server uses the password login and prints a warning on every
login. HTTPS is encrypted in transit and pinned to the server key, but it
terminates at the server or CDN; the request body is visible there. Pass
`--require-hrt` to refuse HTTPS outright.

## Non-guarantees

No absolute security guarantee. A compromised VM, browser session, endpoint, or
binary is outside the tool's protection. See [Security Model](Security-Model).

## Canonical sources

- `tools/hrt-tool/docs/PRIVACY_ENVIRONMENTS.md`
- `docs/hrt-protocol/DEV_GUIDE.md`
