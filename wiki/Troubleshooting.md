# Troubleshooting

## Exit codes

| Code | Name | Meaning |
| --- | --- | --- |
| 0 | `OK` | success |
| 1 | `INTERNAL` | unexpected internal error |
| 2 | `USAGE` | invalid command line or input value |
| 3 | `VAULT` | local vault access or state error |
| 4 | `NETWORK` | transport or remote failure |
| 5 | `DATA` | malformed or inconsistent data |
| 6 | `NOT_IMPLEMENTED` | reserved or unimplemented feature |
| 7 | `AUTH` | server authentication or authorization failure |
| 8 | `TRUST` | security-policy refusal |
| 9 | `PERMISSION` | local file permission or overwrite refusal |
| 10 | `CONFLICT` | concurrent modification or duplicate |
| 11 | `CANCELLED` | user declined or cancelled |
| 12 | `CONFIG` | configuration key or value error |
| 13 | `F2F` | F2F library error |
| 14 | `PLATFORM` | feature unavailable in this build |

`hrt-tool help exit-codes` prints the same table.

## First checks

```sh
hrt-tool doctor
hrt-tool version
hrt-tool config path
```

`doctor` checks the vault, its permissions, the config directory, proxy and
server policy, trust pins, payload version, and component versions. It does not
print secrets.

## Common errors

| Message | Exit | Fix |
| --- | --- | --- |
| `NOT_LOGGED_IN` | 7 | Run `hrt-tool login`, or pass `--snapshot FILE` to work on a local snapshot. |
| `LOGIN_COOLDOWN` | 7 | A login failed less than 60 seconds ago. Wait for the number of seconds shown, then try again. The pause is kept by the client; the server has its own rate limits. |
| `SNAPSHOT_REQUIRED` | 2 | `export` and `import` always need `--snapshot FILE`. Run `snapshot pull --out FILE` first. |
| `USER_ID_REQUIRED` | 2 | An encrypted snapshot also needs `--user-id ID` and the account password. |
| `SNAPSHOT_CREDENTIALS` | 2 | Local changes are written only to an encrypted snapshot; pass `--user-id` and the account password. |
| `SNAPSHOT_EXISTS` | 9 | `snapshot pull --out FILE` does not overwrite a file. Pass `--force` or choose another path. |
| `SNAPSHOT_PLAINTEXT` | 5 | The server returned a legacy plaintext snapshot, which `snapshot pull` does not write to a file. Print it with `snapshot pull --out -`. |
| `CONFLICT` | 5 | The account snapshot kept changing while `hrt-tool` tried to write it (three attempts). Run the command again. |
| `MODE_REQUIRED` | 2 | Pass `--mode transfem` or `--mode transmasc`. Live commands and the console also use the mode saved with `config set mode`. |
| `NO_SNAPSHOT` | 5 | The account has no cloud snapshot yet. Save data with cloud sync in the web app first. |
| `TIME`, `DURATION` | 2 | Use a format from [Time input](hrt-tool-Reference#time-input). |
| `WATCH` | 2 | `status --watch` needs a positive number of seconds. |
| `SERVER_MISMATCH` | 8 | The session belongs to another server. Use `login --server URL` to switch. |
| `SERVER_PROBE` | 4 | The server did not answer `GET /api/tool/v1/version` with `200`. Check the URL and the proxy, and that the server offers the tool API. |
| `HRT_URL` | 2 | An `hrt://` server URL takes no path: `hrt://HOST[:PORT]`. |
| `HRT_REQUIRED` | 2 | `--require-hrt` was given but the server is not `hrt://`. |
| `DEVICE_NAME` | 2 | `--device-name` takes 1 to 64 printable characters. |
| `PROXY_URL` | 2 | Only `socks5h://` and `socks5://` proxy URLs are accepted. |
| `TOR_PROXY_MISSING`, `I2P_PROXY_MISSING` | 4 | `.onion` and `.i2p` need a proxy: run `proxy tor` or `proxy i2p`. |
| `TOR_PROXY_FAILED`, `I2P_PROXY_FAILED` | 4 | The proxy could not resolve the hidden-service name. Check that Tor or I2P is running, or reconfigure with `proxy tor` or `proxy i2p`. |
| `PROXY_DNS` | 8 | Hidden-service traffic must use a `socks5h://` proxy so the proxy resolves the name. Run `proxy tor` or `proxy i2p`. |
| `TRUST_NO_PIN` | 8 | The tool could not compute a public-key pin for the server and refuses to continue without one. Check the server address and the proxy, then retry. |
| `EXPORT_EXISTS` | 3 | `export` never replaces a file; choose a new `--out` path. |
| `LINK_ACTIVE` | 10 | Pass `--force` to remove the active link; the base link becomes active. |
| `CONFIG_KEY`, `CONFIG_VALUE` | 12 | See [Configuration keys](hrt-tool-Reference#configuration-keys). |
| `TRANSPORT_SELECTION`, `NOT_IMPLEMENTED` | 6 | `openssl-v1` and server compute are reserved. The transport follows the server URL. |

## Vault

| Message | Fix |
| --- | --- |
| `VAULT_MISSING` | Run `hrt-tool init`. |
| `VAULT_PERMISSIONS` | `chmod 600` the vault; it must not be group/world accessible. |
| `VAULT_PASSWORD_PERMISSIONS` | `chmod 600` the password file. |
| Forgot the master password | There is no recovery; restore the vault file from your own backup. |

## Trust

`TRUST_CHANGED` (exit 8) means the server presented a different public key from
the one pinned for that host and port. It can be a planned key rotation or an
interception, so do not re-trust blindly. Confirm the new SPKI SHA-256 with the
server operator out of band, then re-run the command with `--trust-server`.
`--yes` never accepts a changed key. A renewed certificate with the same key
keeps working without any action.

## Network and proxy

- `proxy test` sends an unauthenticated `GET /api/tool/v1/version` to the server
  saved in the vault, through the configured proxy, with a fixed 5-second
  timeout; it ignores `--server`, `--timeout`, and the active link. Without a
  proxy it fails with `PROXY_MISSING` (exit 2); an answer other than `200`
  fails with `PROXY_TEST` (exit 4).
- Use `socks5h://` or `socks5://` (rewritten to `socks5h`); HTTP(S) and SOCKS4
  proxies are refused.
- Environment proxy variables are ignored. Set the proxy explicitly with
  `proxy tor`, `proxy i2p`, or `config set proxy socks5h://HOST:PORT`. The
  `proxy` command itself accepts only `tor`, `i2p`, `off` (or `none`),
  `status`, and `test`.
- `--debug` or `--trace` prints libcurl connection diagnostics for `https://`
  and `http://` servers (an `hrt://` connection prints none) with credential
  headers redacted, disables retries, and suppresses the human failure line.
- Read commands retry a tunnel interruption twice. Mutations never auto-retry;
  if a write may have been applied, check the account before retrying.

## F2F

- `F2F_DISABLED` means the build used `HRT_TOOL_ENABLE_F2F=OFF`. Rebuild with
  the Go c-archive enabled.
- Passphrase and token files must be mode `0600` on Unix.
- Verify member fingerprints in person; a clean keyring import is not identity
  proof.

## Logs

Debug logs are encrypted. Inspect one with:

```sh
hrt-tool logs show --log-file <path>
```

The log file and the vault are separate; keep both if you need support.

## Build failures

- Missing OpenSSL 3, libcurl, nlohmann/json, Rust, or Go: see
  [Build and Install](Build-and-Install).
- Windows and 32-bit targets: see [Cross Compilation](Cross-Compilation).
- Sanitizer builds require GCC or Clang.

## Reporting

Do not include real health data, keys, tokens, or passphrases in an issue.
Report security problems privately; see
[Reporting Vulnerabilities](Reporting-Vulnerabilities).

## Canonical sources

- `tools/hrt-tool/src/exit_codes.cpp`
- `tools/hrt-tool/src/doctor.cpp`
- `SECURITY.md`
