# Using hrt-tool

`hrt-tool` is the terminal client. It keeps an encrypted local vault, talks to
the tracker over HTTPS or the raw `hrt/1` transport, and links the F2F keyring
commands as `hrt-tool f2f ...`.

## First run

```sh
hrt-tool init
hrt-tool config path
hrt-tool version
```

`init` prompts for a master password twice. The password must reach at least
40 bits under both the website's guess model and the uniform model. The vault
is written to `<config-dir>/vault.hrtv` with mode `0600` on Unix.

Automation can supply the password with `--vault-password-stdin` or
`--vault-password-file FILE` (mode `0600`). Secrets are never taken from
environment variables or command-line arguments.

## Commands

| Command | Purpose |
| --- | --- |
| `console` | Interactive `hrt>` / `hrt$link>` console. |
| `init [--reset]` | Create or replace the vault. |
| `login`, `logout`, `whoami` | Account session. |
| `snapshot pull\|push\|list` | Encrypted snapshot sync. |
| `look`, `status [all]` | Current and expected concentrations, plus labs. |
| `add`, `dose`, `lab`, `weight` | Local or live records. |
| `add-link URL NAME`, `commit-server NAME` | Named account contexts. |
| `link list\|show\|rename\|remove` | Manage links. |
| `proxy tor\|i2p\|off\|status\|test` | SOCKS5 proxy shortcuts. |
| `config get\|set\|unset\|path` | Server, proxy, language, welcome, humor, mode. |
| `doctor` | Vault, proxy, server policy, trust pins, versions. |
| `export`, `import` | Encrypted `HRTE` export/import. |
| `completion bash\|zsh\|fish` | Static completion script. |
| `f2f ...` | F2F keyring commands. |
| `help [COMMAND\|exit-codes]` | Help and the exit-code table. |
| `version [--json]` | Tool, core, printer, and F2F versions. |

## Console and links

```text
hrt> add-link hrt://hrt.example.org:9443 mtf
hrt> commit-server mtf
hrt$mtf> status all
hrt$mtf> add dose
hrt$mtf> exit
```

`exit` in `hrt$name>` returns to the base link and persists it; `Ctrl-D` keeps
the current link. `commit-server -` returns to the base link. The active link
also applies to one-shot commands. `link show` prints only redacted session
state.

## status

```sh
hrt-tool status              # four offsets
hrt-tool status all          # 1m/5m/10m/30m/1h/2h/4h/8h/12h/24h/2d
hrt-tool status --at 1h,2h
hrt-tool status --labs 3
hrt-tool status --watch 10
hrt-tool status --json
```

Recent labs show estradiol in pmol/L and pg/mL, testosterone in ng/dL and
nmol/L, and the assay method.

## add dose

`hrt-tool add dose` starts an interactive interview: drug, dose, time, route, a
`[Yes/No]` summary, then the saved record id, `hrt:YES`, the timestamp/user
block, and a phase progress line. `add dose --debug` shows raw transport
diagnostics. A failed save never prints `100% done` and is never retried
automatically.

## Proxy

```sh
hrt-tool proxy tor       # socks5h://127.0.0.1:9050
hrt-tool proxy i2p       # socks5h://127.0.0.1:4447
hrt-tool proxy off
hrt-tool proxy status
hrt-tool proxy test
hrt-tool proxy socks5://127.0.0.1:9050
```

`socks5://` is rewritten to `socks5h://`, so the proxy resolves every name and
local DNS is not used for hidden services. HTTP(S) and SOCKS4 proxies are
refused. Environment proxy variables are ignored.

## Diagnostics

- `--debug` / `--trace` print raw connection diagnostics, suppress the human
  failure line, and disable retries.
- `--no-humor` suppresses only the human line.
- Reads retry a tunnel interruption twice; mutations never auto-retry.
- `hrt-tool help exit-codes` prints the stable exit-code table. See
  [Troubleshooting](Troubleshooting).

## Canonical sources

- `tools/hrt-tool/README.md`
- `tools/hrt-tool/docs/PRIVACY_ENVIRONMENTS.md`
