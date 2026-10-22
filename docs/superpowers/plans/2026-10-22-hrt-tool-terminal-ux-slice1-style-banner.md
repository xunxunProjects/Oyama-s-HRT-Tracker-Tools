# hrt-tool terminal UX, slice 1: capabilities, style, banner

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `hrt-tool` decides per stream whether colour, escapes and Unicode are safe, can style text by role, measures display width, and shows an ASCII welcome banner in the interactive console.

**Architecture:** A new pure module `term` (capabilities from an injectable environment snapshot, role styling, UTF-8 display width) plus a thin process adapter (isatty, window size, env, Windows VT and code page). A new `banner` module renders the wordmark and chooses a wide or compact layout from `caps.columns`. `run_console` asks the capabilities of `out`, prints `console_welcome(...)`, and `clear` uses `caps.ansi`.

**Tech Stack:** C++17 (no new dependency), POSIX `isatty`/`ioctl(TIOCGWINSZ)`, Win32 console API behind `#ifdef _WIN32`, CMake, the repo's `tests/test_framework.h`.

**Spec:** `docs/superpowers/specs/2026-10-22-hrt-tool-terminal-ux-design.md` (sections 6 and 7). Later slices (tables, languages, CSV) get their own plans.

## Global Constraints

- Work on branch `feat/hrt-tool-terminal-ux`; no push, no merge to `main`; leave the audit plan `2026-10-21-hrt-repos-audit` and `wiki/` alone.
- No new dependency, no process spawn (`system`, `popen`, `exec`), no emoji, no 256-colour or RGB.
- Existing tests stay green (baseline: 40 of 40 pass). `--json` and non-TTY output must stay byte-identical to 0.1.1.
- Colour = ANSI 16 only: Title `ESC[1m`, Accent `ESC[36m`, Muted `ESC[2m`, Ok `ESC[32m`, Warn `ESC[33m`, Error `ESC[31m`, every run ends `ESC[0m`.
- `NO_COLOR` counts when non-empty (even `0`); `--no-color` means no escapes at all; no `FORCE_COLOR`.
- Source style: global scope, no namespaces, snake_case functions, PascalCase types, 4-space indent, `#pragma once`. New sources go in the `hrtcpp` list in alphabetical order; each new test uses the 3-line `add_executable` / `target_link_libraries(... hrtcpp)` / `add_test` pattern inside `if(BUILD_TESTING)`.
- Build and test commands (run from the worktree root, tests serially): `cmake -S tools/hrt-tool -B tools/hrt-tool/build`, `cmake --build tools/hrt-tool/build -j$(nproc)`, `ctest --test-dir tools/hrt-tool/build --output-on-failure`.

## Review Focus

1. Piped or redirected output never carries an escape or the banner (pinned in Task 2 and Task 3).
2. `NO_COLOR` semantics: empty means colour on, any non-empty value including `0` means off, and the `ansi` flag stays true so `clear` still works (Task 1).
3. Narrow terminals (columns 1 to 51, and a hand-built caps with 0 or a negative width) pick the compact layout without crashing or wrapping an unsigned subtraction (Task 3).
4. A non-ASCII hint (Cyrillic now, CJK later) is measured in display columns and never forces the compact layout (Task 1 width tests, Task 3 layout test).
5. Junk `COLUMNS`, `LANG`, `TERM` values (`abc`, `0`, `-5`, `99999`, `12x`, empty) fall back to safe defaults (Task 1).

---

### Task 1: terminal capability and style core

**Files:**
- Create: `tools/hrt-tool/src/term.h`, `tools/hrt-tool/src/term.cpp`, `tools/hrt-tool/tests/term_test.cpp`
- Modify: `tools/hrt-tool/CMakeLists.txt` (add `src/term.cpp` after `src/status.cpp`; register `term_test` with the 3-line pattern)

**Interfaces:**
- Produces (`term.h`, global scope):
  - `struct TermCaps { bool tty = false; bool ansi = false; bool color = false; bool faint = false; bool unicode = false; int columns = 80; };`
  - `struct TermEnv { bool is_tty = false; int columns_hint = 0; bool vt_ready = true; bool no_color_flag = false; std::map<std::string, std::string> vars; };`
  - `TermCaps term_caps(const TermEnv &env);` pure; rules are the table in spec 7.1.
  - `enum class Role { Plain, Title, Accent, Muted, Ok, Warn, Error };`
  - `std::string term_style(const TermCaps &caps, Role role, const std::string &text);`
  - `std::size_t utf8_width(const std::string &text);`

- [ ] **Step 1: Write `tests/term_test.cpp` (failing) with these functions called from `main`, all using `CHECK_EQ` with explicit `std::size_t{}` for widths**
  - `test_ansi_color`: non-tty env with `TERM=xterm` gives tty, ansi, color, faint all false. tty + `TERM=xterm-256color` gives all four true. `NO_COLOR=1` and `NO_COLOR=0` give ansi true, color false, faint false; `NO_COLOR=""` gives color true. `no_color_flag` gives ansi and color false. `TERM` `dumb`, `""` and unset give ansi false. `TERM=linux` gives ansi and color true, faint false. `vt_ready=false` gives ansi false.
  - `test_columns`: hint 132 beats `COLUMNS=100`; hint 0 with `COLUMNS=100` gives 100; `COLUMNS` of `1000` and `1` accepted; each of `abc`, `0`, `-5`, `99999`, `12x`, `" 100"`, `""` gives 80; no variables gives 80.
  - `test_unicode`: `LC_ALL=C.UTF-8` true; `LC_ALL=""` with `LANG=en_US.utf8` true; `LC_ALL=C` with `LANG=en_US.UTF-8` false; only `LC_CTYPE=de_DE.UTF-8` true; `LANG=en_US.UTF8` true; nothing set false; `TERM=linux` with `LANG=C.UTF-8` false.
  - `test_style`: with a caps value (color, faint) each role wraps text exactly as in Global Constraints (`"\x1b[1mx\x1b[0m"` and so on); Plain returns `x`; faint=false makes Muted return `x`; color=false returns text for every role; empty text returns `""`.
  - `test_width`: `""` 0; `abc` 3; `Привет` 6; `中文` 4; `ひら` 4; fullwidth `Ａ` 2; `e` + U+0301 1; U+200B 0; `"\xff"` 1; `"a\xe4"` (truncated sequence) 2; U+1F600 2; a control byte `"\x07"` 0.
- [ ] **Step 2: Run to verify it fails**
  Run: `cmake -S tools/hrt-tool -B tools/hrt-tool/build && cmake --build tools/hrt-tool/build --target term_test`. Expected: build error, `term.h` not found.
- [ ] **Step 3: Implement `term.h` and `term.cpp`**
  `term_caps` follows spec 7.1. `COLUMNS` is parsed strictly (digits only, at most 4 characters, value 1..1000). `utf8_width` decodes UTF-8 strictly (reject overlongs, surrogates, values above U+10FFFF and truncated sequences; each rejected byte counts 1). Zero width: U+0300-036F, U+200B-200F, U+20D0-20FF, U+3099-309A, U+FE00-FE0F, U+FEFF, and C0 controls and DEL. Width 2: U+1100-115F, U+2E80-303E, U+3041-33FF, U+3400-4DBF, U+4E00-9FFF, U+A000-A4CF, U+AC00-D7A3, U+F900-FAFF, U+FE30-FE6F, U+FF00-FF60, U+FFE0-FFE6, U+1F300-1F64F, U+1F680-1F6FF, U+1F900-1F9FF, U+20000-3FFFD; everything else 1.
- [ ] **Step 4: Run to verify it passes**
  Run: `cmake --build tools/hrt-tool/build --target term_test && tools/hrt-tool/build/term_test`. Expected: `PASS`.
- [ ] **Step 5: Commit**
  `git add tools/hrt-tool/src/term.h tools/hrt-tool/src/term.cpp tools/hrt-tool/tests/term_test.cpp tools/hrt-tool/CMakeLists.txt && git commit -m "feat(hrt-tool): terminal capability and style core"`

---

### Task 2: read the terminal state from the process

**Files:**
- Modify: `tools/hrt-tool/src/term.h`, `tools/hrt-tool/src/term.cpp`, `tools/hrt-tool/src/main.cpp`, `tools/hrt-tool/tests/term_test.cpp`

**Interfaces:**
- Consumes: Task 1 `TermEnv`, `TermCaps`, `term_caps`.
- Produces:
  - `TermEnv term_env_for(const std::ostream &out, bool no_color_flag);` `&out == &std::cout` means fd 1, `&out == &std::cerr` means fd 2, any other stream is not a TTY. Fills `is_tty`, `columns_hint` (ioctl `ws_col`, or Windows `srWindow.Right - srWindow.Left + 1`), `vt_ready` (always true on POSIX; on Windows the console mode has `ENABLE_VIRTUAL_TERMINAL_PROCESSING`), `no_color_flag`, and the six variables when set. On Windows an unset `TERM` becomes `xterm` when the stream is a TTY and VT is ready.
  - `TermCaps term_caps_for(const std::ostream &out, bool no_color_flag);` is `term_caps(term_env_for(...))`.
  - `void term_platform_init();` Windows: enable VT processing on stdout and stderr when they are consoles; when stdout or stderr is a console, switch the output code page to UTF-8 and restore it with `std::atexit`; define `ENABLE_VIRTUAL_TERMINAL_PROCESSING` as `0x0004` if the header lacks it. Other platforms: empty body.
- `main.cpp` calls `term_platform_init();` before `run_cli`.

- [ ] **Step 1: Add POSIX-only tests (`#ifndef _WIN32`) to `term_test.cpp` (failing)**
  `test_process_adapter`: with `setenv("COLUMNS","100",1)` and `setenv("TERM","xterm",1)`, a `std::ostringstream` gives `term_env_for(...).is_tty == false`, `term_caps_for(...)` has `tty`, `ansi`, `color` false and `columns == 100`; `term_env_for(std::ostringstream{}, true).no_color_flag` is true; `term_env_for` copies `NO_COLOR` into `vars` after `setenv("NO_COLOR","1",1)`. (Do not assert about `std::cout`: it is a TTY when the test is run by hand.)
- [ ] **Step 2: Run to verify it fails.** Expected: link or compile error, `term_env_for` undeclared.
- [ ] **Step 3: Implement the three functions and the `main.cpp` call**
- [ ] **Step 4: Run to verify it passes**, then compile-check the Windows paths:
  `x86_64-w64-mingw32-g++ -std=c++17 -Wall -Wextra -Wpedantic -fsyntax-only -Itools/hrt-tool/src tools/hrt-tool/src/term.cpp` and the same with `i686-w64-mingw32-g++`. Expected: no output. If wine runs here, also build `tests/term_test.cpp` with `src/term.cpp` as a static MinGW exe and run it under wine as a bonus check.
- [ ] **Step 5: Commit**
  `git commit -am "feat(hrt-tool): read terminal state from the process; Windows VT and UTF-8 init"`

---

### Task 3: ASCII banner renderer

**Files:**
- Create: `tools/hrt-tool/src/banner.h`, `tools/hrt-tool/src/banner.cpp`, `tools/hrt-tool/tests/banner_test.cpp`
- Modify: `tools/hrt-tool/CMakeLists.txt` (add `src/banner.cpp` after `src/backup_envelope.cpp`; register `banner_test`)

**Interfaces:**
- Consumes: `TermCaps`, `Role`, `term_style`, `utf8_width`.
- Produces:
  - `std::string render_banner(const TermCaps &caps, const std::string &version, const std::string &hint);` returns text ending with `\n`.
  - `std::string console_welcome(const TermCaps &caps, bool json, const std::string &custom, const std::string &hint, const std::string &version);` custom non-empty returns `custom + "\n"`; else `json` or `!caps.tty` returns `hint + "\n"`; else `render_banner(...)`.

- [ ] **Step 1: Write `tests/banner_test.cpp` (failing)**
  Helpers: `plain(columns)` returns caps with `tty` true and the rest false; `strip_ansi(text)` removes `ESC[...m` runs; `split_lines(text)`.
  - `test_wide_plain`: `render_banner(plain(80), "0.1.1", "HINT")` has exactly 6 lines; line 0 is ` _  _ ___ _____`; line 1 starts `| || | _ \_   _|` padded so `hrt-tool 0.1.1` is found at column 19; line 2 holds `Oyama HRT Tracker terminal client` at column 19; line 3 is `|_||_|_|_\ |_|`; line 4 is empty; line 5 is `HINT`; no `\x1b`; no line ends with a space.
  - `test_wide_color`: caps with color and faint at 80 columns: `strip_ansi` of the render equals the plain render; the render contains `\x1b[36m`, `\x1b[1m`, `\x1b[2m`; with `faint=false` it has no `\x1b[2m`.
  - `test_layout_boundary`: version `0.1.1` needs 52 columns: 51 gives 2 lines (compact: `hrt-tool 0.1.1` then the hint), 52 gives 6 lines. A 100-character version at 80 columns is compact. `columns` 1, 0 and -3 are compact and do not crash.
  - `test_hint_width`: a Cyrillic hint at 60 columns still renders wide (6 lines); a hint of 200 characters at 80 columns is still wide.
  - `test_console_welcome`: custom text wins on a TTY (`"custom\n"`); `json=true` on a TTY gives `HINT\n`; non-TTY gives `HINT\n` with no `\x1b`; TTY without `json` gives the banner.
- [ ] **Step 2: Run to verify it fails.** Expected: `banner.h` not found.
- [ ] **Step 3: Implement `banner.cpp`**
  Four wordmark rows exactly as spec 7.4 (16 columns wide, backslashes escaped in C++). Needed width for the wide layout is `16 + 3 + max(utf8_width(title), utf8_width(product))`, compared as `caps.columns > 0 && static_cast<std::size_t>(caps.columns) >= needed`. Style each art row with Accent before padding; pad with plain spaces; never emit trailing spaces.
- [ ] **Step 4: Run to verify it passes**
  Run: `cmake --build tools/hrt-tool/build --target banner_test && tools/hrt-tool/build/banner_test`. Expected: `PASS`.
- [ ] **Step 5: Commit**
  `git add tools/hrt-tool/src/banner.h tools/hrt-tool/src/banner.cpp tools/hrt-tool/tests/banner_test.cpp tools/hrt-tool/CMakeLists.txt && git commit -m "feat(hrt-tool): ASCII welcome banner renderer"`

---

### Task 4: show the banner in the console; gate `clear` on capabilities; docs

**Files:**
- Modify: `tools/hrt-tool/src/console.cpp`, `tools/hrt-tool/tests/console_test.cpp`, `tools/hrt-tool/README.md`, `CHANGELOG.md`

**Interfaces:**
- Consumes: `term_caps_for`, `console_welcome`, `current_version_string()` (`version.h`), `tr(language, I18nKey::Welcome)`.

- [ ] **Step 1: Extend `console_test.cpp` (failing)**
  After the existing run: the output starts with `hrt-tool console. Type 'help' for commands, 'exit' to quit.\n` (string streams are not TTYs) and contains no `\x1b`.
- [ ] **Step 2: Run to verify** the new assertion passes or fails as expected (it should already pass on the old code; it pins the non-TTY output before the change).
- [ ] **Step 3: Edit `console.cpp`**
  Compute `const TermCaps caps = term_caps_for(out, session_options.no_color);` before the welcome; print `console_welcome(caps, options.json, context.session.data.welcome, tr(language, I18nKey::Welcome), current_version_string())`; `clear` emits `\x1b[2J\x1b[H` only when `caps.ansi`; delete `stream_is_tty` and the `<io.h>`/`<unistd.h>` includes if nothing else uses them.
- [ ] **Step 4: Docs.** README: a short "Colour and terminals" section (what is detected, `NO_COLOR`, `--no-color`, the banner and how a custom vault `welcome` replaces it, Windows and kernel-console notes). CHANGELOG: an `## Unreleased` section above `## 0.1.1`.
- [ ] **Step 5: Full verification**
  Build everything and run the whole `ctest` (expected: 42 of 42, the 40 baseline plus `term` and `banner`). Then check in a PTY at `xterm-256color` (look at the banner, `NO_COLOR=1`, `--no-color`, narrow width) and with stdin and stdout piped (`printf 'exit\n' | ... console`: no `\x1b`, one-line welcome).
- [ ] **Step 6: Commit**
  `git commit -am "feat(hrt-tool): show the banner in the console; gate clear on capabilities; docs"`
