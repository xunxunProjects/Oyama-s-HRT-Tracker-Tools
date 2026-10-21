# Security Model

## Assets

- The master password and the derived vault key.
- Account tokens, device keys, and F2F private keys.
- Dose, lab, weight, and account records.
- The server identity and its pinned public key.

## Local vault

- AES-256-GCM payload encryption.
- PBKDF2-SHA256 with 600,000 iterations for the master key.
- Vault and secret files are written with mode `0600` on Unix and replaced
  atomically.
- Session data is wiped on exit; link and device secrets are wiped with the
  session.
- Plaintext secrets are never written to logs. Debug logs are encrypted,
  length-framed records.
- A vault or password file that is readable by group or others is refused.
- Exports never replace an existing file. Plaintext leaves the tool only on
  stdout, for example `export --out -`.
- Local input files are capped at 16 MiB and must be regular files.
- On Windows the vault relies on NTFS ACLs, and vault writes are not yet
  crash-durable.

The vault protects data at rest. It does not protect against a compromised
host, a keylogger, a malicious binary, or an attacker who knows the master
password.

## Server trust

- Ordinary servers need `https://` or `hrt://`. `http://` is accepted only for
  loopback, `.onion`, and `.i2p` hosts, with a warning on every run.
- Before any credential is sent, an unauthenticated trust probe
  (`GET /api/tool/v1/version`) must answer `200`. Redirects are never
  followed.
- On first contact the TLS certificate and SPKI SHA-256 are shown. Typing
  `yes` pins them; `--trust-server` persists the decision
  non-interactively; `--yes` accepts a first-contact key for the current run
  only.
- The pin is enforced on every TLS handshake, including the raw `hrt/1`
  transport. A changed key is refused with `TRUST_CHANGED` until it is
  re-trusted with `--trust-server`; `--yes` never accepts a changed key.
- `--insecure-tls` turns off certificate and hostname checks for one HTTPS run
  and prints a warning. It has no effect on `hrt://`.
- Tokens are origin-bound and stored in the encrypted vault.

## Accounts and login

- The HTTPS login sends the account password to the server. The cloud-data
  key is derived from that password, so a compromised server, or anything that
  logs requests where TLS ends, can derive the key too. Treat HTTPS servers as
  honest-but-curious at best, and do not reuse the account password.
- The `hrt://` login never sends the account password. The account owner
  approves a one-time code in the web app, only the code's SHA-256 is sent,
  and the cloud-data key is derived locally. Every raw request is signed by a
  per-install device key that the owner can revoke.
- The 60-second pause after a failed login is enforced by the client; the
  server applies its own rate limits.
- `logout` always clears the local token, cloud key, and device pass, even
  when server-side revocation fails. `--no-cache-cloud-key` keeps the derived
  key out of the vault when it is passed on every command that may derive it.

See [Using hrt-tool](Using-hrt-tool#logging-in) for both flows.

## Proxies and DNS

- Only `socks5://` and `socks5h://` are accepted; `socks5://` is rewritten to
  `socks5h://`.
- The proxy resolves every name. The tool does not perform local DNS for
  hidden services.
- Environment proxy variables are ignored.
- `.onion` and `.i2p` names are never resolved locally. Without a proxy the
  tool stops with `TOR_PROXY_MISSING` or `I2P_PROXY_MISSING`.
- Tor (`127.0.0.1:9050`) and I2P (`127.0.0.1:4447`) shortcuts are provided.
  `proxy test` verifies the configured proxy.

## F2F keys

- Private keys are generated locally and never uploaded.
- Passphrase and token files must be mode `0600` on Unix.
- Fingerprints must be verified in person; a clean keyring import is not
  identity proof.
- A server operator can enable a cohort escrow key. The pulled cohort keyring
  then includes the escrow public key, and whoever holds the escrow private key
  can decrypt messages encrypted to it. Members' own private keys are never
  escrowed. See [F2F Keyrings](F2F-Keyrings#cohort-escrow-keys).

## Privacy invariants

- No device fingerprinting and no global ban list. The `hrt://` device key is
  a random per-install key pair, not a fingerprint of the hardware, and the
  account owner can revoke it under **Account → Security**.
- The raw `hrt/1` server is opt-in; HTTPS remains the default.
- No shell execution: the tools do not call `system`, `popen`, `exec`, or
  `dlopen`.
- No custom cryptography; OpenPGP is delegated to the selected library.

## Non-goals

- This is not an independent security audit.
- No production-readiness claim, no stable `.hrtb`, and no "no vulnerabilities"
  claim.
- No protection against real health data being included in a test or report.

## Reporting

Do not open a public issue with exploit details, real health data, keys, or
tokens. See [Reporting Vulnerabilities](Reporting-Vulnerabilities) for where to
report, what is in scope, and how to test safely. The policy is
[`SECURITY.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/blob/main/SECURITY.md).

## Canonical sources

- `SECURITY.md`
- `tools/hrt-tool/docs/PRIVACY_ENVIRONMENTS.md`
- `tools/hrt-tool/README.md` (security model)
- `tools/hrt-f2f/README.md`
