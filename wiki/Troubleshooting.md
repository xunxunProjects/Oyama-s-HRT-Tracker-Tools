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

## Vault

| Message | Fix |
| --- | --- |
| `VAULT_MISSING` | Run `hrt-tool init`. |
| `VAULT_PERMISSIONS` | `chmod 600` the vault; it must not be group/world accessible. |
| `VAULT_PASSWORD_PERMISSIONS` | `chmod 600` the password file. |
| Forgot the master password | There is no recovery; restore the vault file from your own backup. |

## Trust

`TRUST_CHANGED` means the server's pinned SPKI key changed. Do not re-trust
blindly. Verify the server out of band, then use `--trust-server` (persist) or
`--yes` (current run only) after confirming the new key.

## Network and proxy

- `proxy test` checks the configured proxy.
- Use `socks5h://` or `socks5://` (rewritten to `socks5h`); HTTP(S) and SOCKS4
  proxies are refused.
- Environment proxy variables are ignored. Set the proxy explicitly with
  `proxy tor`, `proxy i2p`, or `proxy socks5://…`.
- `--debug` or `--trace` prints raw connection diagnostics, disables retries,
  and suppresses the human failure line.
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

Do not include real health data, keys, tokens, or passphrases. Follow
[`SECURITY.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/blob/main/SECURITY.md).

## Canonical sources

- `tools/hrt-tool/src/exit_codes.cpp`
- `tools/hrt-tool/src/doctor.cpp`
- `SECURITY.md`
