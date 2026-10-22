# hrt-tool Reference

Global flags, configuration keys, time input, and file locations for
`hrt-tool`. For task-oriented help, start with [Using hrt-tool](Using-hrt-tool).
Exit codes and error names are listed in [Troubleshooting](Troubleshooting).

## Global flags

Global flags may go before or after the command. A flag that takes a value
accepts `--flag value` or `--flag=value`; a flag that takes no value rejects
one. `--` ends flag parsing, and the next token is the command. An unknown
option before the command is a usage error (`unknown option: X`); after the
command it is passed to that command. `-h` and `--help` print the help text.

Every token after `hrt-tool f2f` is passed unchanged to the F2F commands,
which have their own `--server`, `--proxy`, `--token-file`, and `--timeout`;
see [F2F Keyrings](F2F-Keyrings).

### Server and network

| Flag | Meaning |
| --- | --- |
| `--server URL` | Server for this command. Defaults to the saved `server`, or `https://hrt.mahiro.uk`. An authenticated command refuses another origin with `SERVER_MISMATCH`; switch with `login --server URL`. |
| `--proxy URL` | SOCKS5 proxy for this command. `socks5://` is rewritten to `socks5h://`; HTTP(S) and SOCKS4 proxies are refused with `PROXY_URL`. |
| `--timeout SECONDS` | Network timeout, 1–3600. Default `30`. |
| `--trust-server` | Accept and pin the presented server key without a prompt, including a changed key. Use it only after checking the key out of band. |
| `--insecure-tls` | Turn off CA and hostname checks for HTTPS for this run, with a warning. No effect on `hrt://`, where the pinned key is the trust anchor. |
| `--require-hrt` | `login` refuses to run unless the server is an `hrt://` URL (`HRT_REQUIRED`, exit 2), so the HTTPS password login is never used. |
| `--device-name NAME` | Label for this install's `hrt://` device key: 1 to 64 printable characters (`DEVICE_NAME`, exit 2 otherwise). The web app shows it under **Account → Security → hrt-tool sign-in**, in the sign-in requests and in the device list. That entry appears only when the server has hrt-tool pairing enabled. |
| `--transport auto\|tls\|openssl-v1` | Reserved. The transport follows the server URL; `openssl-v1` exits 6 (`NOT_IMPLEMENTED`). |
| `--compute local\|server` | Reserved. Only `local` is implemented; `server` exits 6. |

### Vault, account, and snapshots

| Flag | Meaning |
| --- | --- |
| `--config-dir DIR` | Use another configuration directory (see [Files and locations](#files-and-locations)). |
| `--vault-password-file FILE` | Read the vault master password from a file of at most 256 bytes. On Linux and macOS the file must be mode `0600` (exit 9 otherwise); the mode is not checked on Windows. |
| `--vault-password-stdin` | Read the vault master password from standard input. |
| `--no-cache-cloud-key` | Do not store the derived cloud-data key in the vault on this command. Pass it to every command that may derive the key. |
| `--snapshot FILE` | Work on a local snapshot file instead of the live account. Accepted by `status`, `look`, `add`, `dose`, `lab`, and `weight`; required by `export` and `import`. |
| `--user-id ID` | Account user id, needed to decrypt an encrypted snapshot. |
| `--account-password-file FILE` | Account password from a file of at most 256 bytes, for the HTTPS login or to derive an encrypted snapshot's key. On Linux and macOS the file must be mode `0600` (exit 9 otherwise); the mode is not checked on Windows. |
| `--account-password-stdin` | Account password from standard input. |
| `--token-file FILE` | Accepted before the command, but no `hrt-tool` command reads it today: the session token is kept in the vault. After `hrt-tool f2f`, the flag belongs to the F2F commands (see [F2F Keyrings](F2F-Keyrings)). |
| `--mode transfem\|transmasc` | Data mode. Without it, live commands and the console use the mode saved with `config set mode`; otherwise the mode declared in the snapshot is used. A command that needs a mode and finds none fails with `MODE_REQUIRED`. |
| `--utc` | Read `--time` values without an offset as UTC, and print times in UTC. |

<!-- DRAFT (Phase 1, read from the source, not run): `--token-file` is parsed
in tools/hrt-tool/src/cli.cpp:103-104, but nothing reads `options.token_file`.
tools/hrt-tool/README.md:386 and tools/hrt-tool/src/completion.cpp:13 still
list it as a global flag. Maintainers should either drop it from those places
or implement it, then revise the row above. -->

### Output and diagnostics

| Flag | Meaning |
| --- | --- |
| `--json` | JSON on standard output for commands that support it, such as `status`, `look`, `doctor`, and `version`. Trust prompts and warnings stay on standard error. |
| `--no-color` | Today this only affects the interactive console: its `clear` command sends the ANSI clear-screen sequence only on a terminal and only without `--no-color`. No other output is coloured. |
| `--no-humor` | Suppress only the human failure line. |
| `--debug`, `--trace` | For `https://` and `http://` servers: libcurl connection and header lines on standard error, with the values of `Authorization`, `Proxy-Authorization`, `Cookie`, and `Set-Cookie` replaced by `<redacted>`. Request and response bodies are never printed. `--debug` also prints one `METHOD URL -> STATUS` line and the server's SPKI SHA-256 for each request. `hrt://` connections print no transport diagnostics. Both flags disable retries and suppress the human failure line. |
| `--log-file FILE` | Record diagnostics in an encrypted log file. Read it with `hrt-tool logs show --log-file FILE`. Without this flag nothing is logged to disk. |

### Confirmations and overwrites

| Flag | Used by |
| --- | --- |
| `--yes` | `link remove`, `import --replace`, and `init --reset` skip the typed confirmation. For trust, `--yes` accepts a first-contact key for the current run only; it never accepts a changed key. |
| `--force` | `add-link` replaces a link with the same name; `link remove` may remove the active link (the base link becomes active); `snapshot pull` may overwrite `--out`. |
| `--probe` | `add-link URL NAME` checks and pins the new link's server immediately and prints the pin on standard error. |

## Configuration keys

```sh
hrt-tool config get [KEY]
hrt-tool config set KEY VALUE
hrt-tool config unset KEY
hrt-tool config path
```

`config path` needs no master password. Every other `config` subcommand asks
for it.

| Key | Values | After `unset` |
| --- | --- | --- |
| `server` | `https://…`; `hrt://HOST[:PORT]` without a path (default port `9443`); `http://` only for loopback (`localhost`, `::1`, `127.0.0.0/8`), `.onion`, and `.i2p` | `https://hrt.mahiro.uk` |
| `proxy` | `socks5h://HOST:PORT`, or `socks5://…` (rewritten to `socks5h://`) | No proxy |
| `active-link` | A link name, or `-` for the base link | Base link |
| `language` | `en` or `ru` | `en` |
| `welcome` | Text the console prints when it starts | Default greeting |
| `humor` | `on`/`true` or `off`/`false` | `on` |
| `mode` | `transfem` or `transmasc` | Not set |
| `transport`, `compute` | `config get transport` prints `hrt/1` for an `hrt://` server and `tls` otherwise; `config get compute` always prints `local`. `config set` and `config unset` exit 6: `TRANSPORT_SELECTION` for `transport`, `NOT_IMPLEMENTED` for `compute`. (`config set` with no value fails first with `CONFIG_VALUE`, exit 12.) | — |

Changing `server` to another origin clears the saved session token, the cached
cloud-data key, and this install's device identity, and prints
`warning: server changed; the saved session and device identity were cleared`
on standard error.

An unknown or missing key fails with `CONFIG_KEY`, and a missing value or an
invalid `language`, `humor`, or `mode` value fails with `CONFIG_VALUE` (both
exit 12). An invalid `server` or `proxy` is a usage error (exit 2):
`SERVER_URL`, `HRT_URL`, `ONION_ADDRESS`, `I2P_ADDRESS`, or `PROXY_URL`.

## Time input

| Form | Examples | Notes |
| --- | --- | --- |
| Now | `now` | |
| Relative | `+2h`, `-30m`, `-1.5d` | One unit: `m`, `h`, or `d`. |
| Day | `2026y10m19d` | Midnight. |
| ISO 8601 | `2026-10-19`, `2026-10-19T08:30`, `'2026-10-19 08:30:15'`, `2026-10-19T08:30Z`, `2026-10-19T08:30+03:00` | Two-digit fields; seconds and fractions optional. Quote a value that contains a space. |
| Comma | `2026,10,19:08:30` | `YYYY,MM,DD:HH:MM`. Translated to ISO 8601 first, so month and day need two digits. |

A time without an offset is local time, or UTC with `--utc`. An explicit
offset always wins. Anything after a valid time is rejected (`TIME`, exit 2).

## Files and locations

| Item | Location |
| --- | --- |
| Configuration directory | `$XDG_CONFIG_HOME/hrt-tool`, else `$HOME/.config/hrt-tool`; `%APPDATA%\hrt-tool` on Windows; or `--config-dir DIR`. `hrt-tool config path` prints it and the vault path. |
| Vault | `<config-dir>/vault.hrtv`, mode `0600`. On Linux and macOS a vault that is group- or world-accessible is refused. |
| Vault lock | `<config-dir>/vault.hrtv.lock`. An empty file, mode `0600`, locked while a command that may change the vault runs, so a second command waits. It stays on disk. Not used on Windows. |
| Snapshot lock | `<snapshot>.lock`, next to the snapshot. An empty file, created and locked only by commands that write the snapshot. It stays on disk. Not used on Windows. |
| Pulled snapshots | Only where `snapshot pull --out` points, mode `0600`. |
| Exports | Only where `export --out` points. An existing file is never replaced. |
| Logs | Only where `--log-file` points. There is no default log location. |

## Canonical sources

- `tools/hrt-tool/src/cli.cpp` (flag parsing and help)
- `tools/hrt-tool/src/commands.h` (defaults)
- `tools/hrt-tool/src/commands.cpp` (`config` keys)
- `tools/hrt-tool/src/local_commands.cpp` (time input, local snapshots)
- `tools/hrt-tool/README.md`
