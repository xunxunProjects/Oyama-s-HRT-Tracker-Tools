# hrt-tool: terminal UX (style layer, banner, tables, languages, CSV)

Date: 2026-10-22
Status: building. The maintainer reviewed the proposal and said to start with
the ASCII banner, then continue with the rest; there was no separate review of
this document.
Repository: xunxunProjects/Oyama-s-HRT-Tracker-Tools
Branch: `feat/hrt-tool-terminal-ux`, cut from `docs/wiki-readme` (64db011), not `main`
Partly supersedes: `2026-10-21-hrt-tool-console-merge-design.md` (see section 9)

## 1. Intent

`hrt-tool` is the maintainer's only interface from now on: no GUI or web
growth. Its human-readable output is plain and uneven. Running the 0.1.1
binary showed: no colour anywhere (`--no-color` only gates `clear`); a `look`
table up to 175 columns wide whose headers drift from their data; a `status`
view that repeats the same cells on every row and ends in `done.` / `---`
noise; lists with no headers; one fact worded four ways across commands;
silent empty states; a 71-line help wall. The maintainer wants output that is
pleasant and not tiring to read: restrained colour, readable tables, a small
ASCII banner, no emoji, a language setting, and CSV export.

## 2. Goals

- One style layer decides, per output stream, whether colour, escape
  sequences and non-ASCII text are safe and how wide the screen is. When in
  doubt it falls back to plain ASCII.
- ASCII welcome banner in the interactive console.
- Tables that stay aligned and fit 80 columns for the default commands.
- One vocabulary for the same fact in every command.
- Language setting: `en`, `ru`, `fr`, `zh-Hans`, `zh-Hant`, `yue`
  (Cantonese), `ja`, with an environment check for the CJK languages.
- CSV export.
- Works in X/Wayland terminals, the Linux kernel console (TERM=linux), and
  Windows consoles.

## 3. Non-goals

- No GUI, web UI, or `--html` export (dropped unless asked).
- No change to `hrt-f2f` (Go pass-through) or `hrt-cold` output.
- No new dependency (no curses, terminfo, crate, or Go module); builds stay
  offline and reproducible.
- No process spawn: `hrt-tool` never calls `system`, `popen`, or `exec`
  (older spec section 5), so no `fc-list`, `tput`, or `stty`. Terminal facts
  come from `isatty`, the window-size ioctl, Win32 console calls, and
  environment variables.
- No emoji. No 256-colour or RGB palette. No full-screen or alternate-screen
  mode. No mouse.
- No translation of `[CODE]` tokens, technical error causes, JSON keys,
  command names, or flag names.

## 4. Anti-goals

The work fails even if it looks nice when any of these is true:

- An escape sequence reaches a pipe, a file, `--json` output, or a dumb
  terminal.
- Colour is the only carrier of a meaning (a warning must still say `warning`).
- `--json` output differs by one byte from 0.1.1, or exit codes or the
  stdout/stderr split change.
- Text is garbled on a legacy Windows console or the Linux kernel console
  because UTF-8 or CJK was assumed.
- The banner breaks scripted console use (stdin from a pipe).

## 5. Constraints

- C11/C++17, CMake >= 3.16, Rust printer built `--offline`; targets are
  linux amd64/386 and windows amd64/386 (MinGW).
- Existing tests stay green. Substrings they assert (`Recent labs:`,
  `Expected`, `Drug`, `pg/mL`) are kept unless a test is deliberately updated
  in the same commit with the reason in the message.
- Plaintext secrets, vault format, trust rules, and the transport are not touched.
- Work stays on the feature branch: no push, no merge to `main`, and the audit
  plan `2026-10-21-hrt-repos-audit` is left alone.

## 6. Decisions

### 6.1 The maintainer decided

- CLI only; export can be shown as CSV tables.
- Scope: `hrt-tool` human output only. `--json` and piped output stay
  byte-identical and uncoloured.
- Colour = the terminal's own 16 ANSI colours by role. No RGB, no background
  blocks, no blinking, faint (SGR 2) rather than bright-black for muted text.
- Tables: thin rules (header plus one rule), no full grid, no zebra stripes,
  numbers right-aligned, widths measured in display columns, unit in the
  header, low-priority columns dropped on narrow terminals, `status` loses
  repeated cells and footer noise, every list command gets headers.
- ASCII decoration only for the banner, section titles, and the progress line.
- Tables stay in the Rust `hrt-print` crate behind a new C ABI function; the
  old one stays.
- Languages as in section 2. For Chinese and Japanese the user must confirm
  the terminal can show them; if something is missing the user is told what
  to install; choosing Chinese triggers the check automatically.
- Do the ASCII banner first.

### 6.2 My calls (override any of them)

- Slices, each shippable alone: (1) terminal capabilities, style, banner;
  (2) tables and human-output overhaul; (3) languages; (4) CSV export. Each
  slice gets its own plan; slices 2 to 4 are outlined in section 8 only.
- Colour roles use weight for hierarchy and colour for meaning only
  (section 7.2). Accent is plain cyan, not bold: many terminals turn bold into
  bright colours, which wash out on light backgrounds.
- No `FORCE_COLOR` / `CLICOLOR_FORCE`: piped output never carries escapes.
- `--no-color` means plain mode (no escapes at all, as the older spec says
  for `clear`); `NO_COLOR` in the environment removes colour only.
- The banner shows only on an interactive terminal (stdout is a TTY) and
  never with `--json`. A custom vault `welcome` text replaces it.
- Line drawing stays ASCII (`-`) in every table.
- The five network-failure joke lines stay English in the new languages
  (they fall back per key) until the maintainer wants them translated.

## 7. Slice 1: capabilities, style, banner

### 7.1 Capabilities (pure function of an environment snapshot)

Inputs: stream is a TTY; column count the terminal reported; Windows VT
ready; `--no-color`; and the variables `NO_COLOR`, `TERM`, `COLUMNS`,
`LC_ALL`, `LC_CTYPE`, `LANG`.

| Capability | Rule |
|------------|------|
| `tty` | the stream is an interactive terminal |
| `ansi` | tty, VT ready, no `--no-color`, and `TERM` set, non-empty, not `dumb` |
| `color` | `ansi` and `NO_COLOR` unset or empty (any non-empty value disables) |
| `faint` | `color` and `TERM` is not `linux` |
| `unicode` | first non-empty of `LC_ALL`, `LC_CTYPE`, `LANG` contains `utf-8` or `utf8` (any case) and `TERM` is not `linux` |
| `columns` | terminal-reported width if > 0, else `COLUMNS` if an integer in 1..1000, else 80 |

The decision is per stream: stdout and stderr are asked separately. A stream
that is not `std::cout` or `std::cerr` is never a TTY (tests pass string
streams). On Windows an unset `TERM` counts as `xterm` once VT is enabled.

### 7.2 Roles

| Role | Sequence | Use |
|------|----------|-----|
| Plain | none | normal text |
| Title | `ESC[1m` | titles, table headers |
| Accent | `ESC[36m` | banner wordmark |
| Muted | `ESC[2m` (none when not `faint`) | secondary text |
| Ok / Warn / Error | `ESC[32m` / `ESC[33m` / `ESC[31m` | PASS / WARN / FAIL and messages |

Every styled run ends with `ESC[0m`. Without `color`, text is returned
unchanged. Empty text gets no sequences.

### 7.3 Display width

`utf8_width` counts display columns: wide CJK and fullwidth forms count 2,
combining marks and zero-width characters 0, invalid bytes 1 each, everything
else 1. It is an approximation of East Asian Width good for the shipped
languages. Layout code must use it, never `size()`.

### 7.4 Banner

Wordmark, 16 columns wide, drawn in plain ASCII (figlet "small" shapes):

```
 _  _ ___ _____
| || | _ \_   _|
| __ |   / | |
|_||_|_|_\ |_|
```

Wide layout (needs `columns >= 16 + 3 + max(width(title), width(product))`,
52 for version `0.1.1`): the wordmark on the left in Accent; to its right,
on the second row `hrt-tool <version>` (Title), on the third row
`Oyama HRT Tracker terminal client` (Muted, never translated); then a blank
line and the translated hint line (Plain). Compact layout (anything
narrower): `hrt-tool <version>` (Title), then the hint line. No trailing
spaces on any line. The hint's width does not affect the layout choice, so a
Russian or Chinese hint cannot force the compact form.

### 7.5 Where it plugs in

- `console_welcome(caps, json, custom, hint, version)`: custom text wins
  (printed exactly as before), then `json` or no TTY gives the old one-line
  welcome, otherwise the banner.
- `run_console` asks for capabilities of `out` and prints that. `clear` uses
  `caps.ansi` instead of its own TTY check.
- On Windows `main` calls `term_platform_init()` first: switch the console
  output code page to UTF-8 (restored at exit) and enable VT processing on
  stdout and stderr when they are consoles.

## 8. Later slices (outline only)

- **Slice 2, tables and output.** New C ABI function in `hrt-print` taking
  options (width, unicode, colour roles); the old function stays. `look`
  gets aligned numeric columns, units in the header, and drops low-priority
  columns by width. `status` becomes one block per time, not one row per
  offset; footer noise goes. `dose list`, `lab list`, `link list`, `doctor`,
  `config get`, `help exit-codes` get headers and alignment; `doctor` colours
  PASS/WARN/FAIL. One vocabulary (assay, units, time suffix, placeholders).
  Help gets sections and per-command detail; errors gain a hint line;
  empty states say what to do next; `status --watch` redraws in place on an
  ANSI terminal. The `add dose` result block and progress line are
  restyled. Windows glyph and width behaviour is checked statically only.
- **Slice 3, languages.** `Language` grows to the seven tags; accepted
  aliases (`zh-cn`, `zh-tw`, `zh-hk`, `cantonese`, `jp`); every key falls
  back to English when a language has no entry. Translated: banner hint,
  table headers, section titles, doctor checks, empty states, error hints,
  prompts, and the one-line command descriptions in `help`. Environment
  check for `zh-*`, `yue`, `ja`: (1) the locale is UTF-8, (2) `TERM` is not
  `linux` or `dumb` (the kernel console cannot draw CJK; a framebuffer or
  graphical terminal can), (3) a glyph probe line the user confirms they can
  read (for `yue` it includes characters outside plain Chinese, such as
  U+55C5, U+54D7, U+5514). On failure the effective language falls back to
  English and the message lists exactly what is missing and how to fix it
  (UTF-8 locale, a CJK font package, a terminal that can render CJK). Reading
  the font list is not possible without a process spawn, so the probe is the
  font test.
- **Slice 4, CSV export.** `export` gains a CSV format (RFC 4180, header row,
  UTF-8, stdout or the existing file option). Text cells that start with
  `=`, `+`, `-`, `@`, tab, or CR are prefixed with `'` so a spreadsheet
  cannot run them; numeric cells are never prefixed. Plaintext stays on
  stdout or an explicit file, as today.

## 9. Superseded parts of the earlier spec

- Section 6.4 `status` layout (the issue's sample): replaced in slice 2; the
  `--json` object stays. The `Expected` header is kept (a test asserts it).
- Sections 2 and 3, "English and Russian only, no Chinese strings":
  replaced in slice 3; `i18n_test` assertions about `zh` and CJK change then.
- Section 6.2 `clear`: still limited to a TTY without `--no-color`; now
  decided by the capability layer, so `TERM` unset or `dumb` also disables it.

## 10. Verification limits

No Linux kernel console and no Windows machine is available here. The
capability logic for `TERM=linux` is unit-tested; Windows code is
compile-checked with MinGW (`-fsyntax-only`) and not run; visual checks run in
a PTY under `xterm-256color`. Each slice's report states this.
