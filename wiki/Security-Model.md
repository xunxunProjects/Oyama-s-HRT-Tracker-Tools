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

The vault protects data at rest. It does not protect against a compromised
host, a keylogger, a malicious binary, or an attacker who knows the master
password.

## Server trust

- On first contact the TLS certificate and SPKI SHA-256 are shown. Typing
  `yes` pins them; `--trust-server` persists the decision
  non-interactively; `--yes` trusts for the current run only.
- The pin is enforced on every TLS handshake, including the raw `hrt/1`
  transport. A changed pin is refused until re-trusted.
- Tokens are origin-bound and stored in the encrypted vault.

## Proxies and DNS

- Only `socks5://` and `socks5h://` are accepted; `socks5://` is rewritten to
  `socks5h://`.
- The proxy resolves every name. The tool does not perform local DNS for
  hidden services.
- Environment proxy variables are ignored.
- Tor (`127.0.0.1:9050`) and I2P (`127.0.0.1:4447`) shortcuts are provided.
  `proxy test` verifies the configured proxy.

## F2F keys

- Private keys are generated locally and never uploaded.
- Passphrase and token files must be mode `0600` on Unix.
- Fingerprints must be verified in person; a clean keyring import is not
  identity proof.

## Privacy invariants

- No device fingerprinting and no global ban list.
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
tokens. Follow [`SECURITY.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/blob/main/SECURITY.md).

## Canonical sources

- `SECURITY.md`
- `tools/hrt-tool/docs/PRIVACY_ENVIRONMENTS.md`
- `tools/hrt-tool/README.md` (security model)
