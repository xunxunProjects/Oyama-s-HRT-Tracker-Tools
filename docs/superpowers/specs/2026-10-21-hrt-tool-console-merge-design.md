# hrt-tool: merged CLI, interactive console, and packaging

Date: 2026-10-21
Status: design for review
Issue: xunxunProjects/Oyama-s-HRT-Tracker#131
Repository: xunxunProjects/Oyama-s-HRT-Tracker-Tools

## 1. Intent

Make `hrt-tool` the single terminal client for the Oyama HRT Tracker Tools:
the account transport that exists today plus the face-to-face OpenPGP tool
(`hrt-f2f`). Add an interactive console, named connection links, a
`status` view that shows current and expected blood concentrations, an
interactive `add dose` flow, Linux CMake installation and update targets,
and a proxy policy that accepts the addresses people actually type while
keeping DNS at the proxy.

The user works from a terminal, often on networks where the website is
slow or only reachable through Tor/I2P. The console must make the common
loop fast: select a saved link, look at concentrations, record a dose,
leave. It must not weaken the existing privacy properties: all local state
encrypted at rest, no plaintext logs, no credential-bearing request to an
untrusted origin, and no local DNS resolution for hidden-service names.

## 2. Goals

- One installed binary named `hrt-tool` that contains both the account
  client and the F2F keyring surface.
- An interactive console with `hrt>` and `hrt$<link>>` prompts.
- `add-link hrt://HOST[:PORT] NAME` saves a named link; `commit-server NAME`
  enters that link; `exit` returns to the base prompt.
- `status` / `status all` prints the current concentration, the expected
  concentration at 1m, 5m, 10m, 30m, 1h, 2h, 4h, 8h, 12h, 24h, and 2d,
  and the three most recent blood tests with both units and the assay.
- Interactive `add dose`: drug, time, route, dose, summary confirmation,
  then a real progress display; `add dose --debug` adds transport/TLS
  diagnostics.
- `proxy socks5://HOST:PORT` (Tor 9050, I2P 4447) is accepted and
  normalised to `socks5h://`, so the proxy resolves every name. HTTP(S)
  proxies and SOCKS4 remain refused.
- Network failures get a short human line (humorous for tunnel resets and
  a dead server) plus the precise technical cause and error code.
  Idempotent reads retry; mutations never retry automatically.
- Console welcome/help available in English and Russian, chosen by the
  user; no Chinese strings.
- `make install` installs into the system prefix; `make update` rebuilds
  the current tree and replaces the installed binary, keeping the cached
  install prefix and leaving the vault untouched.
- F2F remains the tested Go/OpenPGP implementation, compiled as a local
  library (`-buildmode=c-archive`) and called by C++; the installed
  artifact is still only `hrt-tool`.

## 3. Non-goals

- No Windows one-binary build in this change. The existing Windows job
  stays non-blocking; MSVC cannot link a Go c-archive in the same way.
- No port of the OpenPGP implementation to C++/RNP. The Go code remains
  the F2F implementation.
- No Chinese localisation. Only `en` and `ru` built-in strings.
- No automatic language detection. The user chooses with
  `config set language`.
- No network fetch in `make update`; it is a rebuild-and-reinstall, not a
  remote updater.
- No change to the website API, the `hrt://` wire format, or the F2F
  server contract.
- No plaintext secret on disk, in the environment, or in `argv`.
- No shell execution from the console or from `hrt-tool`. The input
  tokenizer has no pipes, redirection, globbing, or variable expansion.

## 4. Anti-goals

The change fails if any of these is true even when the commands work:

- A `.onion` or `.i2p` name is resolved by the local resolver.
- A token, account password, cloud key, device key, or F2F private key is
  written unencrypted or passed in `argv`/the environment.
- `commit-server` sends a saved token to a different origin than the one
  that issued it.
- The console leaks the master password by re-prompting it on every
  command or by storing it in a temporary file.
- `make update` silently installs to a different prefix or overwrites the
  vault/config directory.
- The merged binary makes F2F's private keys reachable by the account
  transport code, the server, or a log.
- A failed save shows `100% done`.
- A failure message hides the technical cause, retries a mutation, or
  reports success after a lost response.

## 5. Constraints and existing properties to preserve

- C11/C++17, CMake >= 3.16, OpenSSL >= 3.0, libcurl, nlohmann/json, Rust
  printer.
- Vault: AES-256-GCM, PBKDF2-SHA256 600k, mode 0600, atomic replace,
  header as AAD.
- Only `socks5h` semantics. libcurl uses
  `CURLPROXY_SOCKS5_HOSTNAME`; the raw `hrt/1` client sends the
  destination name in the SOCKS5 domain-name field.
- Environment proxies are ignored; `no_proxy` cannot bypass an explicitly
  configured proxy.
- Trust pins the SPKI SHA-256; redirects are not followed; tokens are
  origin-bound.
- Plaintext is written only to stdout; logs are encrypted.
- `hrt-tool` does not call `system`, `popen`, `exec`, `dlopen`, or a shell.
  Linking the Go library does not change this: there is no process spawn.
- Existing tests and CI must stay green.

## 6. User-facing design

### 6.1 Command surface

Top-level commands (existing commands keep their current flags):

```
hrt-tool [global flags] <command> [command args]
hrt-tool console
hrt-tool add-link <hrt://HOST[:PORT]> <NAME> [--mode transfem|transmasc] [--proxy URL] [--force]
hrt-tool commit-server <NAME>            # also switches the saved active link
hrt-tool status [all] [--json] [--at OFFSETS] [--labs N]
hrt-tool add dose [flags] [--debug]      # no flags on a TTY: interactive
hrt-tool f2f <keygen|login|register|status|keyring|encrypt|decrypt|version> ...
```

`hrt-tool` with no arguments and a TTY enters the console; with no
arguments and no TTY it prints help. `hrt-tool console` always enters,
even when stdin is a pipe (useful for scripted sessions).

Global flags are recognised before or after the command, so
`hrt-tool add dose --debug` and `hrt-tool status --json` work. Command
flags (`--drug`, `--time`, ...) remain command arguments.

The `f2f` command is a **raw pass-through boundary**. The first `f2f`
token switches `parse_args` into pass-through mode: every following
token, including `--server`, `--proxy`, `--token-file`, `--timeout`,
`--json`, `--debug`, and `-h`/`--help`, is copied verbatim into
`command_args` and handed to the Go dispatcher. Global flags may appear
before `f2f` only; they are not re-emitted into the Go argv. This keeps
F2F's existing flag semantics (1..300 s timeout, `https://`/loopback
`http://` servers, its own token file) intact and makes
`hrt-tool f2f --help` print the Go usage.

`hrt-tool f2f` is the only public F2F surface. The Go binary is no longer
installed separately. The top-level help lists the `f2f` group.

### 6.2 Console state machine

Prompts:

- `hrt>`: base context. Commands use the base link (`""`), which is the
  session a pre-console vault already has, or a fresh unnamed login.
- `hrt$<name>>`: after `commit-server <name>`. Commands use that link's
  saved server, proxy, mode, and session.
- The console starts in the persisted `active_link` (`hrt$<name>>`) when
  one is set, otherwise in `hrt>`.
- `exit` in a link context returns to `hrt>` and persists the base link
  as the active link; it does not log out.
- `exit`/`quit` at `hrt>` leaves the console. `Ctrl-D` leaves the
  console from any context and keeps the current context persisted.
- `commit-server -` (or `commit-server ""`) returns to `hrt>` and
  persists the base link.

Console commands: every top-level command plus `proxy`, `add-link`,
`commit-server`, `debug on|off`, `help`, `exit`, `quit`, `clear`.
`login`, `logout`, `whoami`, `status`, `look`, `add dose`, `dose`,
`lab`, `weight`, `snapshot`, `export`, `import`, `config`, `logs`, and
`f2f` are dispatched through the same code path as the one-shot CLI,
with the console's unlocked vault session and the active link applied.

`f2f` is the exception: it never inherits the active link's server,
proxy, mode, token, or other vault secrets. The console passes the
tokens after `f2f` verbatim to the Go dispatcher. F2F uses its own
`--server`, `--proxy`, and `--token-file` flags exactly as the
standalone tool did. This keeps the account session and the F2F
credential in separate trust domains.

`init` and `init --reset` are refused inside the console
(`CONSOLE_INIT`): the console holds an unlocked session, and replacing
the vault underneath it is unsafe. The message tells the user to leave
the console and run the command from a shell.

`debug on|off` sets `GlobalOptions.debug` for the rest of the console
session. It is separate from `add dose --debug`, which applies to one
command. The console never persists debug mode.

Input is tokenised by a small shell-like splitter: whitespace separates
arguments; single and double quotes group; backslash escapes the next
character; no expansion. Unmatched quotes are a usage error that does not
exit the console. A line beginning with `#` is a comment.

`help` prints the command list and the current context. `clear` clears
the screen with ANSI escapes only when stdout is a TTY and
`--no-color` is not set.

### 6.3 Links and `commit-server`

A link is a named account context, not just a URL:

```
hrt-tool add-link hrt://hrt.example.org:9443 mtf
hrt-tool add-link hrt://example.onion:9443 tor --proxy socks5://127.0.0.1:9050/
hrt-tool commit-server mtf
```

`add-link` accepts the same server schemes as the rest of the tool:

- `hrt://HOST[:PORT]`, default port 9443;
- `https://HOST[:PORT]`, default port 443;
- `http://HOST[:PORT]` only for loopback, `.onion`, and `.i2p` hosts,
  default port 80.

It applies the existing onion v3/I2P address policy, validates and
normalises the proxy, accepts an optional default mode, and stores the
link in the encrypted vault. It does **not** send credentials. It may run
the existing trust probe only when `--probe` is given, so adding a link
offline works; `--trust-server` behaviour is unchanged elsewhere.

A new link starts with an empty proxy and an empty mode unless `--proxy`
or `--mode` is given. It never inherits the current link's proxy or mode;
silently reusing a Tor proxy or a PK mode for a different server is a
mistake that is hard to notice.

Duplicate names are refused unless `--force` is given; `--force` replaces
the link record but leaves the old session secrets out of the new record.
The base link (`""`) can be updated with `config set server` and cannot be
replaced by `add-link`.

`commit-server NAME` selects the link and persists `active_link`. In the
console it changes the prompt. It refuses an unknown name and prints the
known names. `commit-server ""` or `commit-server -` returns to the base
context and persists it.

The persisted `active_link` also applies to one-shot commands:
`hrt-tool commit-server mtf` followed by `hrt-tool status` uses the `mtf`
link. The console starts in the persisted link. Exiting a link with
`exit` persists the base link; leaving the console with `Ctrl-D` keeps
the current link. `--server`/`--proxy`/`--mode` on a command override the
link for that invocation only, subject to the existing origin-mismatch
check for tokens.

A link's mode is set with `add-link --mode`, `config set mode
transfem|transmasc`, or a per-command `--mode`. `config get mode` prints
the active link's mode. A link named `mtf` does **not** imply
`transfem`; the name is only a label. If no mode is configured, the
existing default (transfem) and the payload's declared mode apply.

Each link has its own session material: token, user id, username, device
pass, cached cloud key, device key/certificate/fingerprint, failed-login
time. `login` inside a link context logs into that link. Switching links
therefore does not send one origin's token to another origin, and
switching back does not require a new approval.

The base link is the entry named `""`. A vault created before this change
migrates its top-level session into `links[""]`.

### 6.4 `status`

`status all` mirrors the issue's table, with the prompt's context:

```
Current time              Drug-Categories             Timing-of-Medication           blood drug concentration            Expected
----------------------------------------------------------------------------------------------------------------------------------------------------------
UTC +8 12:00            Estradiol valerate\2mg    1d , 11h , 10min                  169.0 pg/mL                          1min 165.0 pg/mL
UTC +8 12:00            Estradiol valerate\2mg    1d , 11h , 10min                  169.0 pg/mL                          5min 160.0 pg/mL
...
done.
---
Three recent blood tests:
Estradiol level: 620.0 pmol/L \ 169.0 pg/mL
Testosterone: 30.0 ng/dL \ 1.04 nmol/L
Testing method:
Mass spectrometry
done
```

- One row per modelled compound and offset. `Drug-Categories` is the
  display name plus the last dose (`Estradiol valerate\2mg`).
  `Timing-of-Medication` is the time since the last dose of that
  compound, formatted as `Xd , Yh , Zmin`.
- `blood drug concentration` is the current value; `Expected` is the
  value at the offset, prefixed by the offset label. Offsets default to
  `1m,5m,10m,30m,1h,2h,4h,8h,12h,24h,2d`; `--at` overrides them.
- `Current time` is local time by default, printed as `UTC ±H:MM HH:MM`;
  `--utc` prints `UTC +0`.
- `status` without `all` prints the current row, the first four offsets,
  and three labs. `status all` prints every offset and every stored lab.
- Labs are sorted newest first. E2 values print both `pmol/L` and
  `pg/mL`; T values print both `ng/dL` and `nmol/L`; the assay prints
  `Mass spectrometry` for `lcms`, `Immunoassay` for `immuno`.
- The layout follows the issue's multi-line sample, with the `\`
  separator between drug and dose and between the two lab units. It does
  not reuse `look`'s one-line table.
- `--json` emits a stable object:

  ```json
  {
    "now": "UTC +8 12:00",
    "mode": "transfem",
    "weightKg": 70,
    "compounds": [ /* as look */ ],
    "labs": [ /* as look */ ],
    "rows": [
      {
        "drug": "Estradiol valerate",
        "dose": "2mg",
        "lastDose": "1d , 11h , 10min",
        "now": "169.0 pg/mL",
        "offset": "1min",
        "expected": "165.0 pg/mL"
      }
    ],
    "warnings": [ /* as look */ ]
  }
  ```

  `rows` is derived from `compounds` and the selected offsets; it is not
  a second simulation. `warnings` stays in the JSON for parity with
  `look`; in table mode the uncalibrated-baseline notice goes to stderr
  so stdout pipes cleanly.
- `status` uses the live session when `--snapshot` is absent, and the
  local snapshot otherwise, exactly like `look`.

### 6.5 Interactive `add dose`

`add dose` with no dose flags on a TTY starts the interview. It also
starts inside the console when no flags are given. With flags it behaves
like today's `add` and does not prompt.

`hrt-tool add --drug ... --time ... --measurement ...` remains the
canonical one-shot form and is unchanged. `hrt-tool add dose ...` is the
interactive form; `hrt-tool dose add ...` remains the existing alias.
`add --help` prints the one-shot flags; `add dose --help` prints the
interview and flag forms. Scripts keep using `add`, so existing fixtures
and README examples do not break.

Prompts and validation:

```
Drug Categories:
> Estradiol valerate
Dosage:
> 2mg
Timing of Medication:
> 2026,10,20:18:29
How to take the medication:
> injection
```

- Drug accepts a known drug name or a unique prefix; unknown names are
  rejected with the list of accepted names. A name containing the dose
  (for example `Estradiol valerate 2mg`) fills both fields.
- Dosage accepts `mg`, `ug`/`mcg`, and `g`, using the existing parser.
- Time accepts `now`, `+2h`/`-30m`, `YYYY-MM-DD HH:MM`,
  `YYYY,M,D:HH:MM` (the issue's format), and ISO 8601 with an optional
  offset. An explicit offset names the instant it denotes; `--utc`
  controls display only.
- Route is a menu with the existing routes; the default is the drug's
  default route and is shown in the prompt. `patch remove` is only
  offered for patch events.
- `--extras key=value` remains available for the advanced PK fields.

Summary and confirmation:

```
Time of medication (2026-10-20 18:29) Type of medication (Estradiol valerate 2mg injection)..........[Yes/No]:
```

`yes`/`y` saves; `no`/`n` returns to the first prompt with the previous
answers as defaults. `--yes` skips the confirmation for scripts.

On a successful save:

```
<record id>
hrt:YES
-------------------------------------
- Timestamp: October 20, 2026, 18:29
- User: <username or user_<id>>
-------------------------------------
Recording...
........................................................... 100% done
```

The progress line reports real phases, not a timer: `connecting`,
`trust`, `fetching snapshot`, `applying`, `uploading` for live mode;
`reading`, `applying`, `writing` for local snapshot mode. A failure prints
the failed phase and exits non-zero; it never prints `100% done`.

`add dose --debug` (or a console `debug on`) prints the existing
transport/TLS diagnostics to stderr while the interview and save run.
`--debug` is a global flag and is accepted after the command.

### 6.6 Proxy

`proxy` command in the console and `config set proxy` / `--proxy` accept:

- `socks5h://HOST[:PORT]`
- `socks5://HOST[:PORT]` — rewritten to `socks5h://` and a one-line note
  is printed. The stored value is the rewritten one.
- Default port 1080 when omitted.

Everything else (`http://`, `https://`, `socks4://`, malformed URLs) is
refused with `PROXY_URL`. `.onion` and `.i2p` targets still require a
proxy and the rewritten scheme. The raw `hrt/1` client and libcurl both
use the proxy's name-resolution path; the local resolver never sees the
destination name. The existing tests for `PROXY_DNS`,
`TOR_PROXY_MISSING`, `I2P_PROXY_MISSING`, and env-proxy ignoring stay.

### 6.7 Language and welcome

`config set language en|ru` selects the console language. Default `en`.
The welcome banner, console `help`, common console errors, and the
prompt help are localised. Command output, tables, JSON, and error codes
stay English so scripts and documentation remain stable. No Chinese
strings are shipped. A user can also set a custom welcome line with
`config set welcome "..."`; it is printed instead of the built-in
welcome. The custom line is stored in the encrypted vault.

### 6.8 F2F surface

```
hrt-tool f2f keygen ...
hrt-tool f2f login ...
hrt-tool f2f register ...
hrt-tool f2f status ...
hrt-tool f2f keyring list|merge|pull ...
hrt-tool f2f encrypt ...
hrt-tool f2f decrypt ...
hrt-tool f2f version
```

The flags and semantics are exactly `hrt-f2f`'s current flags and
semantics, including `--server`, `--token-file`, `--proxy`,
`--timeout`, `--json`, `--force`, and the mode-0600 rules. The Go
`status` is namespaced, so it does not collide with the new top-level
`status`.

F2F is explicitly exempt from links: it never reads the vault's active
server, proxy, mode, or token, and `add-link`/`commit-server` do not
change its defaults. This is a credential-separation rule. A user who
wants Tor for F2F passes `--proxy socks5://127.0.0.1:9050/` (the Go
client normalises it to `socks5h` with the same rule as the C++ client).
F2F's `--server` remains `https://` or loopback `http://`; an `hrt://`
link URL is not valid for F2F and is not passed through.

`hrt-tool version` adds an `f2f: <version>` line from the linked library.

### 6.9 Install and update

CMake installs only the merged binary and its documentation:

```
cmake -S tools/hrt-tool -B tools/hrt-tool/build \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build tools/hrt-tool/build --parallel
sudo cmake --install tools/hrt-tool/build
```

A source-tree `tools/hrt-tool/Makefile` wraps the same commands:

```
make            # configure + build
make test       # build + ctest
make install    # configure + build + cmake --install
make update     # rebuild + cmake --install using the cached prefix
make clean
```

`PREFIX=/usr/local` is the install prefix; `BUILD_DIR=build` overrides
the build directory. `make install` reconfigures so a changed `PREFIX`
takes effect. `make update` does **not** reconfigure and does **not**
fetch source; it uses the prefix in `build/CMakeCache.txt`, rebuilds the
`hrt-tool` target and its Go/Rust dependencies, and installs over the
existing binary. It never touches the vault, config, or snapshot files.
If no build directory exists, it prints the `make install` command and
exits non-zero.

The CMake project also gets a custom `update` target with the same
build-then-install behaviour for people who drive CMake directly.

### 6.10 Failure messages, humour, and retry policy

Network failures get a short, human line in the console language plus
the precise technical line and error code. One-shot invocations use
English for the human line, because the vault may not be unlocked when
the failure is printed; the console uses the configured en/ru language.
The humour never replaces the diagnosis, and it is never used for
authentication, trust, or data integrity failures.

Classification (from the TCP/TLS/SOCKS5 result):

- **Tunnel interrupted** — `ECONNRESET`, `EPIPE`, unexpected EOF while
  reading a TLS record, a SOCKS5 failure after the handshake, a receive
  timeout after the connection was established.
  - EN: `The connection was unexpectedly closed or reset.`
  - EN: `Tunnel dropped. Fuck! Disconnecting at a time like this? Fine
    — let me reconnect, and I’ll settle the score.`
  - RU: `Соединение неожиданно закрыто или сброшено.`
  - RU: `Туннель упал. Чёрт! Отключаться именно сейчас? Ладно —
    переподключусь и поквитаюсь.`
- **Server-initiated error** — use this **only when there is clear
  evidence**: `ECONNREFUSED`, an HTTP status line was actually received
  (`4xx`/`5xx`, including `502`/`503`/`504`), or a TLS alert/fatal alert
  was received. A bare TCP reset is tunnel interruption, not a server
  refusal.
  - EN: `The server rejected the connection.`
  - EN: `The server’s not playing nice today. Let me check the error
    logs.`
  - RU: `Сервер отклонил соединение.`
  - RU: `Сервер сегодня не в настроении. Проверю логи ошибок.`
- **Connection timed out** — connect or read timeout before a response.
  - EN: `Still waiting for the tunnel...`
  - EN: `Hello? Is anyone there on the other end of the tunnel? Let’s
    try again.`
  - RU: `Всё ещё жду туннель...`
  - RU: `Алло? Есть кто-нибудь на другом конце туннеля? Давай
    попробуем ещё раз.`
- **Proxy down** — the configured SOCKS5 listener does not accept the
  connection.
  - EN: `The Tor proxy at socks5h://127.0.0.1:9050 isn't answering. Is
    the daemon actually running, or did it wander off?`
  - RU: `Прокси Tor по адресу ... не отвечает. Демон точно запущен или
    ушёл погулять?`
- **Final fallback** — after the retries are exhausted, or the network
  failure does not fit another class.
  - EN: `Oh no, the other side doesn't want us anymore (cries).`
  - RU: `О нет, другая сторона больше не хочет с нами разговаривать
    (плачет).`

Rules:

- Idempotent reads (`status`, `look`, `snapshot list`, the snapshot GET
  inside a mutation) retry a tunnel interruption up to two times with
  0.5 s and 1.5 s backoff. The first failure prints the class line
  (`Tunnel dropped...`); each retry prints `Still waiting for the
  tunnel...`; if both retries fail, the final line is
  `Oh no, the other side doesn't want us anymore (cries).` followed by
  the technical cause.
- Mutations (`add`, `add dose`, `dose edit/delete`, `lab`, `weight`,
  `snapshot push`) are **not** retried automatically. A reset after the
  server accepted the write would duplicate a dose or a lab. The message
  says so and tells the user to run `dose list`/`lab list` before
  retrying.
- Auth (`401`/`403`), trust (`TRUST_CHANGED`), origin mismatch, and
  vault/crypto errors always use the existing precise message, never a
  joke.
- The technical line is always printed:
  `hrt-tool: TUNNEL_INTERRUPTED: recv: Connection reset by peer [NETWORK]`.
  The humorous line is printed before it.
- `--debug` (and `--trace`) is the pure diagnostic mode: **no humorous
  line, no automatic retry, and no progress animation**. It prints the
  raw connection/TLS/SOCKS5 diagnostics and the technical error line,
  then exits with the normal exit code. This is the mode to use when the
  user wants to see exactly what the transport did.
- `--no-humor` and `config set humor off` disable only the human line and
  keep the technical line; retries still apply. Default is on.
- The console shows the same lines; `add dose --debug` adds the transport
  diagnostics around the failure and follows the no-retry rule.

Implementation: a small `net_error` module maps a libcurl result, a raw
client error, a SOCKS5 reply, an `errno`, and an HTTP status to
`FailureKind`; an i18n table holds the en/ru lines; the progress helper
and the top-level error printer use it. Unit tests cover each mapping,
the en/ru golden lines, `--no-humor`, read retries, and mutation
no-retry.

### 6.11 Additional commands

These are small, safe additions that fit the console workflow:

- `links` / `link list` — list saved links with name, scheme/host,
  mode, proxy, and logged-in state. Secrets are never printed.
- `link show NAME` — URL, proxy, mode, username, and redacted session
  state (`token: set`, `device key: present`). No secret value is shown.
- `link rename OLD NEW` — rename a non-base link.
- `link remove NAME` — remove a non-base link after a typed `yes`;
  clears that link's secrets. Refuses the base or active link unless
  `--force`; `--force` on the active link first switches to the base.
- `proxy tor` / `proxy i2p` / `proxy off` — set
  `socks5h://127.0.0.1:9050`, `socks5h://127.0.0.1:4447`, or clear.
- `proxy status` — print the normalised configured proxy and which
  network it looks like.
- `proxy test` — connect to the proxy and fetch the unauthenticated
  `/api/tool/v1/version` through it. It never sends a token or reads
  the vault's session, so it cannot leak credentials. It reports
  whether the destination name was resolved at the proxy.
- `doctor [--json]` — check the vault file and mode, config directory
  permissions, proxy configuration and reachability, server URL policy,
  trust-pin presence, vault payload version, C core/Rust printer/F2F
  library versions, and whether the environment proxy variables are
  being ignored. It prints PASS/WARN/FAIL and never prints a secret.
- `status --watch SECONDS` — re-render `status` every N seconds until
  Ctrl-C. It is read-only; useful right after a dose. It refuses a
  non-positive interval and stops cleanly on the first fatal error.
- `completion bash|zsh|fish` — print a static completion script for the
  top-level commands and global flags. No network, no secrets.
- `help [command]` — context help; `hrt-tool help status` is
  `hrt-tool status --help`.
- `version --json` — the existing version data in machine-readable form.

Safety rules for these commands:

- No `shell`, `exec`, plugin system, or history-rewriting command.
- Destructive operations (`link remove`, `config unset`) print exactly
  what they will delete and require confirmation unless `--yes`.
- `link show`, `doctor`, and `proxy test` never print or transmit a
  token, password, cloud key, device key, or F2F private key.
- New commands get a help entry, a test, and an entry in the README.

### 6.12 Exit codes

Every error path returns one of these. The bracketed detail code (for
example `[PROXY_DNS]`) stays stable too; the exit code is the category a
script can branch on. `--help`, `--version`, and a clean `exit` return
`0`.

| Code | Name | Meaning | Examples |
| ---: | --- | --- | --- |
| 0 | `OK` | success | normal command, `--help`, `version` |
| 1 | `INTERNAL` | unexpected internal error | uncaught exception, bug, F2F bridge panic |
| 2 | `USAGE` | invalid command line or input value | unknown command/flag, bad `--mode`, bad time/dose |
| 3 | `VAULT` | local vault access/state | missing vault, wrong master password, weak password, vault lock |
| 4 | `NETWORK` | transport or remote failure | DNS, connect, reset, TLS, timeout, proxy down, HTTP `5xx` |
| 5 | `DATA` | malformed or inconsistent data | invalid JSON, corrupt snapshot/export/log, merge failure |
| 6 | `NOT_IMPLEMENTED` | reserved or unimplemented feature | server compute, reserved transport |
| 7 | `AUTH` | server authentication/authorization | not logged in, `401`/`403`, 2FA, login cooldown, pairing denied |
| 8 | `TRUST` | security-policy refusal | SPKI changed, trust refused, `SERVER_MISMATCH`, `PROXY_DNS` |
| 9 | `PERMISSION` | local file permission/overwrite | mode `0600` violation, existing output without `--force`, stat/lock failure |
| 10 | `CONFLICT` | concurrent modification or duplicate | snapshot `409` after retries, duplicate link without `--force` |
| 11 | `CANCELLED` | user declined or cancelled | confirmation `no`, `init --reset` aborted |
| 12 | `CONFIG` | configuration key/value error | unknown config key, invalid `language`/`humor` value |
| 13 | `F2F` | F2F library error | any non-usage error returned by `hrt-tool f2f` |
| 14 | `PLATFORM` | feature unavailable in this build | F2F disabled, Windows F2F follow-up |

Notes:

- Codes `2`–`6` keep their current numeric values, so most existing
  scripts keep working. `1` becomes the catch-all instead of a generic
  runtime failure.
- Reclassified on purpose: `ApiAuthError`, `TWO_FACTOR_REQUIRED`,
  `LOGIN_COOLDOWN`, and `PAIRING_REQUIRED` move to `7`; `TRUST_*`,
  `SERVER_MISMATCH`, and `PROXY_DNS` move to `8`; `CONFLICT` moves to
  `10`; `RESET_ABORTED` moves to `11`; config key/value errors move to
  `12`.
- `hrt-tool f2f` maps the Go exit code: `0` → `0`, `2` → `2` (Go
  usage), any other non-zero → `13`.
- The console keeps running after a command failure and does not change
  its exit code; leaving with `exit`/`Ctrl-D` returns `0` unless the
  console itself could not start.
- `Ctrl-C` is a signal, not an exit code: the shell reports `130`. The
  tool does not catch it to print a different number.
- The table is printed by `hrt-tool help exit-codes` and documented in
  the README.

## 7. Internal design

### 7.1 Vault schema v2 (links and language)

The vault payload version becomes 2. Version 1 is still accepted and is
upgraded on the next save.

```json
{
  "version": 2,
  "language": "en",
  "welcome": "",
  "active_link": "",
  "links": {
    "": {
      "url": "https://hrt.mahiro.uk",
      "proxy": "",
      "mode": "",
      "token": "",
      "user_id": "",
      "username": "",
      "device_pass": "",
      "cloud_key": "",
      "device_key_pem": "",
      "device_cert_pem": "",
      "device_fingerprint": "",
      "last_login_failed_at": 0
    },
    "mtf": {
      "url": "hrt://host:9443",
      "proxy": "socks5h://127.0.0.1:9050",
      "mode": "transfem",
      "token": "",
      "user_id": "",
      "username": "",
      "device_pass": "",
      "cloud_key": "",
      "device_key_pem": "",
      "device_cert_pem": "",
      "device_fingerprint": "",
      "last_login_failed_at": 0
    }
  },
  "server": "...",
  "proxy": "...",
  "token": "...",
  "...": "existing top-level fields, kept as the active-link view"
}
```

- `links` is the source of truth. The existing top-level session fields
  remain as a view of `links[active_link]` so the current command code
  keeps working.
- On load, if `links` is absent, create `links[""]` from the top-level
  fields. If `active_link` is absent, use `""`.
- On save, copy the top-level session fields into
  `links[active_link]`, then serialise. The trust store stays at the
  top level (pins are per origin and are not link-specific).
- `add-link` inserts a record with empty secrets and does not change
  `active_link`.
- `commit-server` first writes the current view into the current link,
  then sets `active_link`, then overlays the new link into the top-level
  view.
- `exit` from a link writes the current view into that link, sets
  `active_link` to `""`, and overlays the base link. `Ctrl-D` writes the
  current view and leaves `active_link` unchanged.
- `logout` clears only the active link's session fields.
- `config set server` inside a link updates that link's `url`; it still
  clears the session when the origin changes, as today.
- Secrets never leave the encrypted vault; the JSON above is the
  plaintext inside the AES-GCM envelope.

Migration tests must cover: v1 vault opens, the base link is usable, a
v1 `login` token still works, and saving writes v2 without losing fields.
An older `hrt-tool` opening a v2 vault must fail cleanly with
`unsupported vault payload version` rather than misread it. (Older
binaries only accept version 1; this is acceptable because the vault is
upgraded by the new binary and downgrades are not supported. The README
will say so.)

### 7.2 Console session and vault unlock

The current code unlocks and derives the vault key on every command. The
console must do that once. Introduce an explicit `SessionContext`:

```cpp
struct SessionContext {
    VaultSession session;
    std::string master;
    bool loaded = false;
};
```

`dispatch` gains an optional `SessionContext *`. `unlock_full` and
`unlock_required` use the context when present:

- If `ctx->loaded`, return an `UnlockedVault` that references
  `ctx->session` (no PBKDF2, no prompt).
- Otherwise unlock as today and, when a context is supplied, move the
  session/master into it.

`UnlockedVault` becomes a small owner-or-view wrapper. Mutations happen
through the same `VaultSession::data` object, so the context stays current
when a command saves. `Vault::save` is unchanged.

The console unlocks once at startup (`init` first if the vault is
missing), keeps the context in memory, and wipes it on exit. A standalone
one-shot command never creates a context and behaves exactly as today.

The console performs a `VaultLock` per command as today; that is cheap and
keeps cross-process safety.

### 7.3 Global flag parsing

`parse_args` currently treats an unknown `-`-prefixed token after the
command as a command argument. Change it to recognise a fixed set of
global flags at any position, while leaving everything else in
`command_args`. The global set is the list in README §Commands plus
`--debug`, `--trace`, `--json`, `--yes`, `--utc`, and the password
flags.

The exception is `f2f`: once that command token is seen, parsing stops
and every remaining token goes into `command_args` verbatim. F2F really
does share spellings with the global parser (`--server`, `--proxy`,
`--token-file`, `--timeout`, `--json`, `--help`), and its semantics
differ (1..300 s timeout, `https://`/loopback `http://` servers, its own
token file). Without the pass-through boundary, `hrt-tool f2f login
--server ... --token-file ...` would lose its required flags. Global
flags written before `f2f` still apply to the C++ side; they are not
forwarded to Go.

This makes `add dose --debug` and `status --json` behave as expected
without each command re-implementing them; F2F keeps its own flag
handling.

### 7.4 Status renderer

Refactor `render_look` into:

- `build_pk_view(payload, options, current, offsets)` returning the
  existing JSON view model.
- `render_status_view(view, options, all)` producing the table and lab
  block.

The PK engine and age clearance adjustment are unchanged. `status` reuses
the same `run_pk` call and `class_value`/`format_concentration` helpers.
Add `format_offset_label` and `format_elapsed` (`Xd , Yh , Zmin`).
`format_lab_value` already returns both units; add a `\`-separated
variant for status and an assay display map.

Time: compute the local UTC offset with `localtime_r`/`gmtime_r` (or the
Windows equivalent), format `UTC +H:MM` / `UTC -H:MM`. `--utc` forces
`UTC +0`.

### 7.5 Interactive add dose and progress

Add `interactive_add_dose(parsed, in, out, err, session)` in a new
`console_commands.cpp` (or `add_interactive.cpp`). It builds the same
`ParsedArgs`-equivalent flag set that `apply_add` already consumes, then
calls the existing live/local save path. No duplicate dose logic.

A `Progress` helper has `begin(phase)`, `advance(percent)`, `done()` and
`fail(phase, error)`. It prints to stdout when it is the active mode and
uses `\r` only on a TTY; otherwise it prints phase lines. The live path
accepts an optional `Progress *` and calls `begin`/`advance` around
trust, snapshot fetch, merge, and push. `--debug` does not change the
progress output; it adds the existing stderr diagnostics.

Time parsing adds `YYYY,M,D:HH:MM` and `YYYY,M,D` to `parse_time_hours`,
with the same strict trailing-junk rejection.

### 7.6 Proxy normalisation

Add `normalize_proxy_url(std::string)` in `http.cpp` (exported through
`http.h`):

- Parse the URL.
- `socks5` → rewrite scheme to `socks5h`, keep userinfo/host/port/path
  empty; print the one-line note through the caller's `err`.
- `socks5h` → accept.
- Anything else → `PROXY_URL` with the existing message.
- Default port 1080 if absent.

Call it from `config set proxy`, `--proxy` handling in
`http_options_for`, `add-link`, and the console `proxy` command. Store the
normalised value. `validate_proxy_url` remains as the strict check and is
called after normalisation. The raw client already requires `socks5h`; no
change there.

### 7.7 Go F2F as a linked library

Add `tools/hrt-f2f/lib.go` (package main) with cgo exports:

```go
//export hrtf2f_version
func hrtf2f_version() *C.char

//export hrtf2f_run
func hrtf2f_run(argc C.int, argv **C.char) C.int

//export hrtf2f_run_capture
func hrtf2f_run_capture(argc C.int, argv **C.char,
    out **C.char, outLen *C.size_t,
    errOut **C.char, errLen *C.size_t) C.int

//export hrtf2f_free
func hrtf2f_free(ptr *C.char)
```

`hrtf2f_run` builds a Go `[]string`, calls the existing `run`, and writes
to `os.Stdout`/`os.Stderr`. `hrtf2f_run_capture` calls the same `run` with
`bytes.Buffer`s and returns NUL-terminated copies; the caller frees them
with `hrtf2f_free`. C++ uses the capture form so the console can keep
output ordering and tests can assert on it. Output is capped at 128 MiB;
larger output returns an error instead of exhausting memory.

CMake builds it:

```cmake
find_program(GO_EXECUTABLE go REQUIRED)
set(HRTF2F_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../hrt-f2f)
set(HRTF2F_LIB ${CMAKE_CURRENT_BINARY_DIR}/hrtf2f/libhrtf2f.a)
add_custom_command(
  OUTPUT ${HRTF2F_LIB} ${HRTF2F_LIB}.h
  COMMAND ${CMAKE_COMMAND} -E env CGO_ENABLED=1
          ${GO_EXECUTABLE} build -buildmode=c-archive
          -trimpath -o ${HRTF2F_LIB} .
  DEPENDS ${HRTF2F_DIR}/go.mod ${HRTF2F_DIR}/go.sum
          ${HRTF2F_DIR}/*.go
  WORKING_DIRECTORY ${HRTF2F_DIR}
  COMMENT "Building the Go F2F library")
```

`hrt-tool` links the archive and adds `pthread`, `dl`, and `m` (already
present). The Go header is included from the binary directory.

C++ dispatch:

```
command == "f2f" -> build argc/argv from command_args verbatim, call
hrtf2f_run_capture, print stdout/stderr, return the Go exit code.
```

`command_args` is the raw pass-through slice from `parse_args`; the
dispatcher does not prepend `--server`, `--proxy`, `--token-file`,
`--timeout`, or `--json`, and does not read the vault or the active
link. `hrt-tool f2f` with no subcommand and `hrt-tool f2f --help` both
reach the Go usage text.

The Go library is bundled into the binary; no `hrt-f2f` executable is
installed. The F2F tests stay Go tests and run via CTest with
`go test ./...` (offline, as today). A C++ test calls
`hrtf2f_run_capture("version")` and checks the output. Another test
calls `hrtf2f_run_capture` with `login --server ... --token-file ...`
and asserts the Go flag parser saw the flags (a missing `--token-file`
must produce the Go error, not a C++ usage error).

Windows: `HRT_TOOL_ENABLE_F2F` defaults `ON` for GCC/Clang and `OFF` for
MSVC with a clear configure message; the Windows follow-up note explains
that the F2F surface is not in the Windows binary yet. This keeps the
existing non-blocking Windows job honest.

### 7.8 Console implementation

New files:

- `src/console.h`, `src/console.cpp` — REPL loop, tokenizer, state
  machine, context stack, help.
- `src/i18n.h`, `src/i18n.cpp` — `en`/`ru` strings, no Chinese.
- `src/status.cpp` — status view/table (or extend `local_commands.cpp`).
- `src/add_interactive.cpp` — interview and progress.
- `src/link.cpp`, `src/link.h` — link CRUD, validation, session
  switching.

The console calls `dispatch` with a `SessionContext` and a `ParsedArgs`
built from the tokenised line. For non-`f2f` commands it sets
`options.server`, `options.proxy`, and `options.mode` from the active
link unless the line overrides them. For `f2f` it builds a `ParsedArgs`
with the raw tokens after `f2f` and leaves the link fields untouched.
It does not call `run_cli` (which parses `argv` and prints help on empty
input); it calls `dispatch` directly so errors are reported once and the
prompt continues.

Errors from a command are printed and the console continues; `exit` is
the only intentional way out. A command that asks for input (login,
interactive add dose) reads from the console's input stream.

### 7.9 Packaging details

- `include(GNUInstallDirs)` in `tools/hrt-tool/CMakeLists.txt`.
- `install(TARGETS hrt-tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})`.
- `install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})` and the
  privacy/protocol docs.
- `add_custom_target(update ...)`.
- `tools/hrt-tool/Makefile` as described in §6.9.
- Update `tools/hrt-tool/configure` to pass through `--prefix` and
  `--build-dir` (already supports arbitrary args; document `--prefix`).
- CI: add `actions/setup-go` with the `go.mod` version, keep
  `cargo fetch`, add an install smoke test:
  ```
  cmake --install build --prefix "$PWD/stage"
  test -x stage/bin/hrt-tool
  stage/bin/hrt-tool version
  stage/bin/hrt-tool f2f version
  ```
- The release script (`tools/hrt-f2f/scripts/build-release.sh`) is
  updated to build/install `hrt-tool` instead of a standalone `hrt-f2f`
  binary, or marked as legacy. The merged artifact is named `hrt-tool`.

### 7.10 Code quality and safety rules

- **Single responsibility per new module**: `console` (REPL/tokeniser),
  `link` (CRUD/session switching), `status` (view/model rendering),
  `add_interactive` (interview/progress), `net_error` (failure
  classification), `i18n` (strings), `f2f_bridge` (argv marshalling and
  buffer ownership). No business logic in the console dispatcher.
- **No duplicated behaviour**: `status` reuses the PK view and
  `format_concentration`/`format_lab_value`; `add_interactive` builds the
  same flag set and calls the existing `apply_add`; `f2f_bridge` only
  copies argv and owns the returned buffers.
- **Memory and ownership**: RAII for Go capture buffers, file
  descriptors, locks, and vault sessions. No raw `new`/`delete` in new
  code. `hrtf2f_free` is called in a destructor, including on error
  paths.
- **Secrets**: never in `argv`, environment, exception text, logs, or
  `link show`/`doctor` output. The existing request-header redaction
  stays. The Go library receives only the F2F user's explicit flags; it
  never receives vault secrets.
- **Error handling**: typed `ToolError` with stable codes; no exceptions
  crossing the C ABI; the Go bridge catches panic and returns a
  non-zero code with a `F2F_INTERNAL` error instead of aborting the
  process.
- **Warnings**: add `HRT_TOOL_ENABLE_WARNINGS` (default ON) with
  `-Wall -Wextra -Wpedantic` for GCC/Clang and `/W4` for MSVC, and
  `HRT_TOOL_ENABLE_WERROR` (default OFF, ON in CI for Linux) so new
  warnings do not land.
- **Sanitizers**: add `HRT_TOOL_ENABLE_SANITIZERS` for
  AddressSanitizer/UndefinedBehaviorSanitizer, and a CI job that runs
  the C++ tests with F2F disabled (`-DHRT_TOOL_ENABLE_F2F=OFF`) under
  ASan/UBSan. The Go archive is excluded from that job because the Go
  runtime has its own memory management.
- **Tests**: every new module has unit tests; every new command has a
  help entry and at least one success and one failure test. Golden
  outputs cover the status table and the en/ru failure lines.
- **Docs**: README, `--help`, and the design's command table stay in
  sync; new error codes are listed in the README.
- **No process spawn**: the Go bridge is a direct function call. The
  existing no-`system`/`popen`/`exec`/`dlopen` rule still holds.

### 7.11 Exit-code implementation

Extend `ExitCode` in `src/error.h`:

```cpp
enum class ExitCode : int {
    Ok = 0,
    Internal = 1,
    Usage = 2,
    Vault = 3,
    Network = 4,
    Data = 5,
    NotImplemented = 6,
    Auth = 7,
    Trust = 8,
    Permission = 9,
    Conflict = 10,
    Cancelled = 11,
    Config = 12,
    F2F = 13,
    Platform = 14,
};
```

- `run_cli`'s catch-all `std::exception` returns `Internal` (1).
- `ApiAuthError`, `ApiTwoFactorRequired`, `LOGIN_COOLDOWN`,
  `PAIRING_REQUIRED`, and pairing denial use `Auth` (7).
- `TRUST_*`, `SERVER_MISMATCH`, `PROXY_DNS`, and `HRT_TRUST` use
  `Trust` (8).
- Overwrite refusals, mode-0600 violations, and stat/lock failures use
  `Permission` (9).
- `ApiConflict` and duplicate-link errors use `Conflict` (10).
- `RESET_ABORTED` and declined confirmations use `Cancelled` (11).
- `CONFIG_KEY`/`CONFIG_VALUE` use `Config` (12).
- The F2F bridge maps Go `0`/`2`/other to `0`/`2`/`13`; a recovered Go
  panic returns `13` with `F2F_INTERNAL`.
- `Platform` (14) is for a feature that is compiled out (F2F disabled,
  Windows follow-up), not for a user mistake.
- Tests assert the mapping for each error class and the process exit
  status. README and `--help` carry the table; the detailed `[CODE]`
  strings remain unchanged.

## 8. Testing

Unit and integration tests added or extended:

- `proxy_test.cpp`: `socks5://` → `socks5h://`, trailing slash, default
  port, HTTP/HTTPS/SOCKS4 rejection, stored value normalised.
- `net_error_test.cpp`: tunnel reset/EOF/timeout, connection refused,
  `502`/`503`/`504`, other `5xx`, proxy down; en/ru golden lines;
  `--no-humor`; read retries twice; mutations never retry; `--debug`
  prints no humorous line and performs no retry.
- `exit_code_test.cpp`: every error class maps to its documented code,
  the CLI catch-all returns `1`, and the F2F bridge maps `0`/`2`/other
  to `0`/`2`/`13`.
- `time_test.cpp`: `2026,10,20:18:29`, ISO 8601, `now`, offsets,
  trailing junk.
- `vault_test.cpp`: v1 → v2 migration, base link, link switching,
  session isolation, logout clears only the active link.
- `status_test.cpp`: golden table for a fixture payload, both lab unit
  pairs, assay labels, `status` vs `status all`, `--json`.
- `console_test.cpp`: tokenizer (quotes, escapes, comments, unmatched
  quotes), prompt state machine, `exit` returns from a link, `exit` at
  base exits, unknown command does not exit, scripted session.
- `cli_test.cpp`: global flags after the command (`add dose --debug`,
  `status --json`).
- `link_test.cpp`: add-link URL validation, duplicate/`--force`, unknown
  commit-server, onion/I2P proxy requirements, `link list`/`link show`
  redaction, rename, remove confirmation, and active-link refusal.
- `doctor_test.cpp`: PASS/WARN/FAIL paths, no secret values in the
  output, and environment proxies reported as ignored.
- `completion_test.cpp`: each shell emits a non-empty static script and
  no command text is executed.
- `f2f_binding_test.cpp`: `hrtf2f_version`, `hrtf2f_run_capture("version")`,
  `--help` exit code, capture buffers freed.
- Existing Go tests remain the F2F correctness suite: `go test ./...`
  with `GOPROXY=off`.
- Integration with the existing loopback API test server: login, status,
  interactive add dose (scripted stdin), `commit-server`, `exit`.
- Install/update smoke test in CI as above. A local test touches a source
  file and runs `make update` into a staging prefix, then checks the
  binary timestamp/version.

All tests must pass with `ctest --test-dir build --output-on-failure`.
CI stays path-filtered; the workflow is updated for Go and the install
step.

## 9. Phasing

1. **Foundations** — vault v2 links/language migration, global flag
   parsing, proxy normalisation, `SessionContext` unlock refactor.
2. **Console** — tokenizer, REPL, prompts, `help`, i18n en/ru,
   `add-link`, `commit-server`, `proxy`.
3. **Status and add dose** — status renderer, time format, interactive
   interview, progress, `--debug` after the command.
4. **F2F merge** — Go c-archive, C++ dispatch, `hrt-tool f2f`, version
   line, binding tests.
5. **Packaging** — CMake install, Makefile, CI, docs, README/CHANGELOG.
6. **Verification** — full test suite, loopback integration, install and
   update smoke tests, manual console transcript.

Each phase builds and tests independently. The F2F merge is last so the
console and status work can land even if the Go c-archive needs a
follow-up on a platform.

## 10. Decisions made by the builder

These are calls I made; they are not user-approved details:

- Links are full account contexts with independent sessions, not just
  named URLs. The base context is `links[""]`.
- `active_link` is persisted. The console starts in it; `exit` from a
  link persists the base link; `Ctrl-D` keeps the current link.
- F2F is namespaced `hrt-tool f2f ...`; no top-level F2F commands. It is
  a raw pass-through boundary and never inherits link server, proxy,
  mode, or token.
- `add-link` accepts `hrt://`, `https://`, and loopback/hidden-service
  `http://`; a new link inherits neither proxy nor mode.
- A link's mode is explicit (`add-link --mode`, `config set mode`, or
  `--mode`); the name `mtf` does not imply `transfem`.
- The old `hrt-tool add --drug ...` stays canonical; `add dose` is the
  interactive form; `dose add` remains an alias.
- `status` is a new command, not an alias of `look`; `status all` is the
  full table. The layout follows the issue's sample, including the `\`
  separators.
- `status --json` uses the documented `rows` array, derived from the
  same view model as `look`.
- `config set welcome` is included; it overrides the localised banner.
- The console has `debug on|off`; `init`/`init --reset` are refused
  there.
- `make update` is rebuild-and-reinstall with the cached prefix, never
  git-pull and never a reconfigure.
- `socks5://` is rewritten to `socks5h://` with a note; it is not
  rejected.
- Network-failure lines are humorous in en/ru using the user's wording:
  `Tunnel dropped. Fuck!...`, `The server's not playing nice today...`,
  `Hello? Is anyone there...`, and the final
  `Oh no, the other side doesn't want us anymore (cries).` The technical
  cause and code always follow. `--no-humor`/`config set humor off`
  disables the human line. Mutations do not auto-retry.
- Additional commands: `link list|show|rename|remove`, `proxy
  tor|i2p|off|status|test`, `doctor`, `status --watch`, `completion`,
  `help`, and `version --json`.
- Code quality: single-purpose modules, no duplicated dose/PK logic,
  `-Wall -Wextra -Wpedantic` with `-Werror` in Linux CI, an ASan/UBSan
  CI job with F2F disabled, and tests plus help entries for every new
  command.
- `--debug`/`--trace` is the pure diagnostic mode: no humorous line, no
  automatic retry, no progress animation; raw transport diagnostics and
  the technical error line only.
- Exit codes `0`–`14` are defined in §6.12. Codes `2`–`6` keep their
  current values; `1` is the catch-all. The detailed `[CODE]` string is
  unchanged. This is a deliberate reclassification of auth, trust,
  conflict, cancellation, and config errors.
- Interactive `add dose` prompts for the dose separately; a dose embedded
  in the drug string is also accepted.
- Language localises console strings only; command output stays English.
- Windows F2F is deferred.

## 11. Builder-check resolution

A fresh builder review raised nine questions. None is left open; they
are resolved here and in the sections above:

| Question | Resolution |
| --- | --- |
| Who owns F2F flags? | `f2f` is a raw pass-through boundary; globals apply only before it. §6.1, §7.3, §7.7. |
| Does the console apply links to F2F? | No. F2F uses its own server/proxy/token flags. §6.2, §6.8, §7.8. |
| Does the old top-level `add` survive? | Yes, it stays canonical; `add dose` is interactive. §6.5. |
| How is a link's mode set and applied? | Stored per link; `add-link --mode` or `config set mode`; applies to one-shot commands; `--mode` overrides. §6.3, §7.1. |
| What does `active_link` mean across sessions? | Persisted; console starts in it; `exit` persists base; `Ctrl-D` keeps current. §6.2, §7.1. |
| `add-link` schemes and inheritance? | `hrt`/`https`/loopback `http`; no proxy or mode inheritance. §6.3. |
| Status layout and JSON? | Issue sample layout; documented `rows` array. §6.4. |
| Is `debug on|off` in scope? | Yes, console-session scoped. §6.2. |
| Is `init --reset` safe in the console? | No, refused. §6.2. |

The user's follow-up added the exact failure wording and the `--debug`
rule (§6.10), the additional command set (§6.11), the exit-code table
(§6.12, §7.11), and the code-quality rules (§7.10). There are no open
questions in this document.

## 12. Risks

- **Go c-archive portability**: Linux/GCC is the target; MSVC is
  deferred. The Go runtime adds signal handlers and threads to the
  C++ process; test Ctrl-C and normal exits.
- **Vault refactor**: the active-link view keeps command code stable, but
  migration and save ordering need careful tests. A bug could send one
  link's token to another origin; the origin binding check remains the
  last line of defence.
- **Console unlock**: the `SessionContext` refactor touches every command
  that unlocks the vault. Mechanical but broad; the existing test suite
  is the safety net.
- **Progress honesty**: only print 100% after the save returns success.
- **`make update` prefix**: never reconfigure; use the cached prefix.
  Document how to change it (`make install PREFIX=...`).
- **Binary size and startup**: linking Go adds several MiB and some
  startup work. Acceptable for a CLI; measure `hrt-tool version`.
