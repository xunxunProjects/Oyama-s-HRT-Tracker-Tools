# hrt-tool Reference

Global flags, configuration keys, time input, and file locations for
`hrt-tool`. For task-oriented help, start with [Using hrt-tool](Using-hrt-tool).
Exit codes and error names are listed in [Troubleshooting](Troubleshooting).

## Global flags

Global flags may go before or after the command. Every token after
`hrt-tool f2f` is passed unchanged to the F2F commands, which have their own
`--server`, `--proxy`, `--token-file`, and `--timeout`; see
[F2F Keyrings](F2F-Keyrings).

### Server and network

| Flag | Meaning |
| --- | --- |
| `--server URL` | Server for this command. Defaults to the saved `server`, or `https://hrt.mahiro.uk`. An authenticated command refuses another origin with `SERVER_MISMATCH`; switch with `login --server URL`. |
| `--proxy URL` | SOCKS5 proxy for this command. `socks5://` is rewritten to `socks5h://`; HTTP(S) and SOCKS4 proxies are refused with `PROXY_URL`. |
| `--timeout SECONDS` | Network timeout, 1–3600. Default `30`. |
| `--trust-server` | Accept and pin the presented server key without a prompt, including a changed key. Use it only after checking the key out of band. |
| `--insecure-tls` | Turn off CA and hostname checks for HTTPS for this run, with a warning. No effect on `hrt://`, where the pinned key is the trust anchor. |
| `--require-hrt` | `login` refuses to run unless the server is an `hrt://` URL (`HRT_REQUIRED`, exit 2), so the HTTPS password login is never used. |
| `--device-name NAME` | Label for this install's `hrt://` device key, shown under **Account → Security**. |
| `--transport auto\|tls\|openssl-v1` | Reserved. The transport follows the server URL; `openssl-v1` exits 6 (`NOT_IMPLEMENTED`). |
| `--compute local\|server` | Reserved. Only `local` is implemented; `server` exits 6. |

### Vault, account, and snapshots

| Flag | Meaning |
| --- | --- |
| `--config-dir DIR` | Use another configuration directory (see [Files and locations](#files-and-locations)). |
| `--vault-password-file FILE` | Read the vault master password from a mode `0600` file. |
| `--vault-password-stdin` | Read the vault master password from standard input. |
| `--no-cache-cloud-key` | Do not store the derived cloud-data key in the vault on this command. Pass it to every command that may derive the key. |
| `--snapshot FILE` | Work on a local snapshot file instead of the live account. Accepted by `status`, `look`, `add`, `dose`, `lab`, and `weight`; required by `export` and `import`. |
| `--user-id ID` | Account user id, needed to decrypt an encrypted snapshot. |
| `--account-password-file FILE` | Account password from a mode `0600` file, for the HTTPS login or to derive an encrypted snapshot's key. |
| `--account-password-stdin` | Account password from standard input. |
| `--mode transfem\|transmasc` | Data mode. Without it, live commands and the console use the mode saved with `config set mode`; otherwise the mode declared in the snapshot is used. A command that needs a mode and finds none fails with `MODE_REQUIRED`. |
| `--utc` | Read `--time` values without an offset as UTC, and print times in UTC. |

### Output and diagnostics

| Flag | Meaning |
| --- | --- |
| `--json` | JSON on standard output for commands that support it, such as `status`, `look`, `doctor`, and `version`. Trust prompts and warnings stay on standard error. |
| `--no-color` | No colour in terminal output. |
| `--no-humor` | Suppress only the human failure line. |
| `--debug`, `--trace` | Raw transport diagnostics on standard error, with headers redacted. Both disable retries and suppress the human failure line. |
| `--log-file FILE` | Record diagnostics in an encrypted log file. Read it with `hrt-tool logs show --log-file FILE`. Without this flag nothing is logged to disk. |

### Confirmations and overwrites

| Flag | Used by |
| --- | --- |
| `--yes` | `link remove` and `import --replace` skip the typed confirmation. For trust, `--yes` accepts a first-contact key for the current run only; it never accepts a changed key. |
| `--force` | `add-link` replaces a link with the same name; `link remove` may remove the active link (the base link becomes active); `snapshot pull` may overwrite `--out`. |
| `--probe` | `add-link` checks and pins the new link's server immediately and prints the pin. |

## Configuration keys

```sh
hrt-tool config get [KEY]
hrt-tool config set KEY VALUE
hrt-tool config unset KEY
hrt-tool config path
```

| Key | Values | After `unset` |
| --- | --- | --- |
| `server` | `https://…`; `hrt://HOST[:PORT]` without a path; `http://` only for loopback, `.onion`, and `.i2p` | `https://hrt.mahiro.uk` |
| `proxy` | `socks5h://HOST:PORT`, or `socks5://…` (rewritten to `socks5h://`) | No proxy |
| `active-link` | A link name, or `-` for the base link | Base link |
| `language` | `en` or `ru` | `en` |
| `welcome` | Text the console prints when it starts | Default greeting |
| `humor` | `on`/`true` or `off`/`false` | `on` |
| `mode` | `transfem` or `transmasc` | Not set |
| `transport`, `compute` | Shown by `config get`. `config set` exits 6 (`NOT_IMPLEMENTED`). | — |

Changing `server` to another origin clears the saved session and this
install's device identity, and prints a warning. An unknown key fails with
`CONFIG_KEY`; a missing or invalid value fails with `CONFIG_VALUE` (exit 12).

## Time input

| Form | Examples | Notes |
| --- | --- | --- |
| Now | `now` | |
| Relative | `+2h`, `-30m`, `-1.5d` | One unit: `m`, `h`, or `d`. |
| Day | `2026y10m19d` | Midnight. |
| ISO 8601 | `2026-10-19`, `2026-10-19T08:30`, `'2026-10-19 08:30:15'`, `2026-10-19T08:30Z`, `2026-10-19T08:30+03:00` | Two-digit fields; seconds and fractions optional. Quote a value that contains a space. |

A time without an offset is local time, or UTC with `--utc`. An explicit
offset always wins. Anything after a valid time is rejected (`TIME`, exit 2).

## Files and locations

| Item | Location |
| --- | --- |
| Configuration directory | `$XDG_CONFIG_HOME/hrt-tool`, else `$HOME/.config/hrt-tool`; `%APPDATA%\hrt-tool` on Windows; or `--config-dir DIR`. `hrt-tool config path` prints it and the vault path. |
| Vault | `<config-dir>/vault.hrtv`, mode `0600`. A group- or world-readable vault is refused. |
| Snapshot lock | `<snapshot>.lock`, next to the snapshot, while a local command writes it. |
| Pulled snapshots | Only where `snapshot pull --out` points, mode `0600`. |
| Exports | Only where `export --out` points. An existing file is never replaced. |
| Logs | Only where `--log-file` points. There is no default log location. |

## Canonical sources

- `tools/hrt-tool/src/cli.cpp` (flag parsing and help)
- `tools/hrt-tool/src/commands.h` (defaults)
- `tools/hrt-tool/src/commands.cpp` (`config` keys)
- `tools/hrt-tool/src/local_commands.cpp` (time input, local snapshots)
- `tools/hrt-tool/README.md`
