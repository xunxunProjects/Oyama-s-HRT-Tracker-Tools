# Using hrt-tool

`hrt-tool` is the terminal client. It keeps an encrypted local vault, talks to
the tracker over HTTPS or the raw `hrt/1` transport, and links the F2F keyring
commands as `hrt-tool f2f ...`. Every global flag and configuration key is
listed in [hrt-tool Reference](hrt-tool-Reference).

## First run

```sh
hrt-tool init
hrt-tool config path
hrt-tool version
```

`init` prompts for a master password twice. The password must reach at least
40 bits under both the website's guess model and the uniform model. The vault
is written to `<config-dir>/vault.hrtv` with mode `0600` on Unix.

The config directory is `$XDG_CONFIG_HOME/hrt-tool` or `~/.config/hrt-tool`
(`%APPDATA%\hrt-tool` on Windows). `--config-dir DIR` selects another one.

Automation can supply the password with `--vault-password-stdin` or
`--vault-password-file FILE` (mode `0600`). Secrets are never taken from
environment variables or command-line arguments.

Running `hrt-tool` with no arguments in a terminal starts the console with
default options. To give it flags such as `--config-dir`, run
`hrt-tool console --config-dir DIR`. With no arguments and no terminal on
standard input, `hrt-tool` prints the help text instead.

## Commands

Global flags may go before or after the command.

| Command | Purpose |
| --- | --- |
| `console` | Interactive `hrt>` / `hrt$link>` console. |
| `init [--reset]` | Create or replace the vault. |
| `login`, `logout`, `whoami` | Account session. |
| `snapshot pull\|push\|list` | Encrypted snapshot sync. |
| `look`, `status [all]` | Current and expected concentrations, plus labs. |
| `add`, `dose`, `lab`, `weight` | Records, live after login or in a local `--snapshot`. |
| `export`, `import` | Encrypted `HRTE` export/import of a local `--snapshot`. |
| `add-link URL NAME`, `commit-server NAME` | Named account contexts. |
| `link list\|show\|rename\|remove` | Manage links. |
| `proxy tor\|i2p\|off\|status\|test` | SOCKS5 proxy shortcuts. |
| `config get\|set\|unset\|path` | Server, proxy, active link, language, welcome, humor, mode. |
| `doctor [--json]` | Vault, proxy, server policy, trust pins, versions. |
| `logs show --log-file FILE` | Decrypt an encrypted log file. |
| `completion bash\|zsh\|fish` | Static completion script. |
| `f2f ...` | F2F keyring commands; see [F2F Keyrings](F2F-Keyrings). |
| `help [COMMAND\|exit-codes]` | Help and the exit-code table. |
| `version [--json]` | Tool, core, printer, and F2F versions. |

## Choosing a server

```sh
hrt-tool config get server
hrt-tool config set server https://hrt.example.org
hrt-tool config set server hrt://hrt.example.org:9443
hrt-tool config unset server        # back to https://hrt.mahiro.uk
```

- Ordinary remote servers must use `https://`. Plain `http://` is accepted only
  for loopback, `.onion`, and `.i2p` hosts, with a warning on every run.
- An `hrt://` URL selects the raw `hrt/1` transport and must not have a path.
  The port is optional and defaults to `9443`.
- Switching to a different origin clears the saved session and device
  identity, with a warning.
- The token is sent only to the origin that issued it. A different `--server`
  on an authenticated command fails with `SERVER_MISMATCH`; use
  `hrt-tool login --server URL` to switch deliberately.

`hrt://` needs a raw TLS/TCP listener. It is not available on HTTP-only edge
platforms such as Cloudflare Workers and Pages, Vercel Edge Functions, Netlify
Edge Functions, Deno Deploy, AWS Lambda@Edge, or Fastly Compute@Edge; those
deployments keep the HTTPS API. The project ships the protocol, the client, and
a reference adapter, but no public `hrt://` endpoint. Running one is the
operator's responsibility, so ask your operator whether one exists.

## Trusting a server

Before any credential is sent, `hrt-tool` checks the server. For `https://` and
`http://` it makes an unauthenticated `GET /api/tool/v1/version` request, which
must answer `200`. For `hrt://` it makes no HTTP request: it opens a TLS 1.3
connection and takes the public key of the server certificate. On first contact
it prints `first contact with scheme://host:port` and the certificate details,
including the SPKI SHA-256, to stderr, then asks you to type `yes`.

| Situation | Result |
| --- | --- |
| First contact, you type `yes` | The SPKI pin is saved in the vault for that `host:port`. |
| First contact with `--trust-server` | The key is accepted and pinned without a prompt. |
| First contact with `--yes` | The key is accepted for this run only and is not pinned. |
| Renewed certificate, same key | Accepted; the pin is the public key, not the certificate. |
| Changed key | Refused with `TRUST_CHANGED` until you re-trust with `--trust-server`. `--yes` never accepts a changed key. |

The pin is checked on every TLS handshake, including `hrt/1`. Redirects are
never followed. `--insecure-tls` turns off CA and hostname checks for HTTPS on
that run and warns each time; it does not apply to `hrt://`, where the pinned
key is the only trust anchor.

## Logging in

Over HTTPS, `login` uses the legacy password flow and warns about it:

```sh
hrt-tool login --username alice --account-password-file account.txt
```

The account password reaches the server, because HTTPS terminates at the server
or its CDN. A compromised server or log could derive your cloud-data key, so
this flow assumes an honest-but-curious server. Do not reuse the password. A
failed credential attempt pauses login for 60 seconds on the client; the server
applies its own rate limits.

Over `hrt://`, login is approved in the web app and the account password never
leaves the machine:

1. `hrt-tool login --username alice` prints a 128-hex-character code and the
   web address, then waits.
2. In the web app, open **Account → Security → hrt-tool sign-in**, check the
   IP address, country, and device fingerprint the server saw, and enter the
   code. Only the code's SHA-256 is sent. That entry appears only when the
   server has hrt-tool pairing enabled.
3. `hrt-tool` asks for the account password locally to derive the cloud-data
   key.

A code expires after five minutes. A denied or expired code triggers the same
60-second pause. The first `hrt://` login creates a device key and a
self-signed certificate in the vault; every raw request is signed with it, and
the session is bound to that certificate. Revoke a device under
**Account → Security → hrt-tool sign-in**. `--device-name NAME` sets its label
(1 to 64 printable characters). `--require-hrt` refuses to fall back to the
HTTPS password flow.

The vault keeps the token, user id, device pass, and, by default, the derived
cloud key. To keep the key out of the vault, pass `--no-cache-cloud-key` to
every command that may derive it. `logout` revokes the session and always
clears the local token, cloud key, and device pass, even if revocation fails.
The per-install device identity stays.

## Live and local data

After login, `look`, `status`, `add`, `dose`, `lab`, and `weight` work against
the account. A write pulls the newest snapshot, applies the change, merges, and
uploads with a conditional write, retrying on a `409` conflict.

```sh
hrt-tool add --drug "Estradiol valerate" --time now --measurement 2mg \
  --route injection --mode transfem
hrt-tool dose list
hrt-tool dose edit --id <id> --measurement 3mg
hrt-tool dose delete --id <id>
hrt-tool lab add --time now --value 180 --unit pg/ml --assay lcms
hrt-tool weight set --kg 72
hrt-tool look --all --json
```

Snapshots:

```sh
hrt-tool snapshot list
hrt-tool snapshot pull --out snapshot.json     # --force replaces the file
hrt-tool snapshot push --in snapshot.json
```

`snapshot pull` writes only the encrypted cloud bundle, with mode `0600`, and
refuses to replace a file without `--force`. A legacy plaintext revision is
written only to `--out -`. `snapshot push` merges before uploading and accepts
cloud bundles, plaintext sync payloads, `HRTE` exports, and complete-backup
envelopes.

`look`, `status`, `add`, `dose`, `lab`, `weight`, `export`, and `import` accept
`--snapshot FILE` and then work on that file instead of the account. An
encrypted snapshot also needs `--user-id ID` and the account password
(`--account-password-file FILE` or `--account-password-stdin`):

```sh
hrt-tool look --snapshot snapshot.json --user-id <id> \
  --account-password-file account.txt
```

Commands that change a local snapshot hold the advisory lock
`<snapshot>.lock`.

`--time` accepts `now`, relative offsets such as `+2h` or `-30m`,
`2026y10m19d`, and ISO 8601 with an optional `Z` or `±HH:MM`. Times without an
offset are local unless `--utc` is set.

`look` is an uncalibrated baseline: the EKF, OU, and MIPD models are not
ported, and it prints `warning: uncalibrated baseline (calibration is not ported yet)`.
It applies the age-based clearance adjustment but never prints the birth year.

## Export and import

`export` and `import` always work on a local `--snapshot`. To back up the
account, pull first:

```sh
hrt-tool snapshot pull --out snapshot.json
hrt-tool export --out backup.hrte --vault-password-file vault.txt \
  --snapshot snapshot.json --user-id <id> --account-password-file account.txt
hrt-tool import --in backup.hrte --vault-password-file vault.txt \
  --snapshot snapshot.json --user-id <id> --account-password-file account.txt
```

- The account password unlocks the cloud snapshot; the vault master password
  encrypts the `HRTE` file.
- `export --out FILE` writes `HRTE` and refuses to replace a file
  (`EXPORT_EXISTS`). Plaintext goes only to stdout, with `--out -`.
- `import` merges by the website's rules, including tombstones. Records
  missing from the import are not deleted. `import --replace` replaces the
  whole snapshot and needs `--yes` or a typed `yes`.
- Imports are sanitised: unknown routes and out-of-range times are dropped and
  counted, doses are clamped to 0–10000 mg, weights to 20–400 kg, and invalid
  lab units become pmol/L.
- Files over 16 MiB, or with more than 20,000 records in one array, are
  refused.

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

`add-link --probe` runs the trust check for the new link and prints its pin.
`add-link --force` replaces a link with the same name. `link remove NAME` asks
for confirmation unless `--yes` is given.

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
hrt-tool proxy off       # or: proxy none
hrt-tool proxy status
hrt-tool proxy test
hrt-tool config set proxy socks5://127.0.0.1:9050
```

The `proxy` command accepts only `tor`, `i2p`, `off`, `none`, `status`, and
`test`. Set any other SOCKS5 proxy with `config set proxy URL`, or for one
command with `--proxy URL`.

`socks5://` is rewritten to `socks5h://` (default port 1080), so the proxy
resolves every name and local DNS is not used for hidden services. HTTP(S) and
SOCKS4 proxies are refused with `PROXY_URL`. Environment proxy variables
(`http_proxy`, `https_proxy`, `ALL_PROXY`, `no_proxy`) are ignored.

`.onion` and `.i2p` servers need a proxy. Without one, the command stops with
`TOR_PROXY_MISSING` or `I2P_PROXY_MISSING` and names the command to run:

```sh
hrt-tool proxy tor
hrt-tool login --server http://<56-character-address>.onion --username alice \
  --account-password-file account.txt
```

## Diagnostics

- `--debug` / `--trace` print raw connection diagnostics, suppress the human
  failure line, and disable retries.
- `--no-humor` suppresses only the human line.
- Reads retry a tunnel interruption twice; mutations never auto-retry.
- Nothing is logged to disk by default. `--log-file FILE` writes an encrypted
  log; read it with `hrt-tool logs show --log-file FILE`.
- `hrt-tool help exit-codes` prints the stable exit-code table. See
  [Troubleshooting](Troubleshooting).

## Canonical sources

- `tools/hrt-tool/README.md`
- `tools/hrt-tool/docs/PRIVACY_ENVIRONMENTS.md`
- `tools/hrt-tool/src/cli.cpp`, `src/commands.cpp`, `src/local_commands.cpp`,
  and `src/proxy_commands.cpp`
