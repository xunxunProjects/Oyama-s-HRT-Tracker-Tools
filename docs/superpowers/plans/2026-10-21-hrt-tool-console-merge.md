# hrt-tool Merged CLI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Merge `hrt-f2f` into a single `hrt-tool` binary, add an interactive `hrt>` console with named links, a `status` concentration view, interactive `add dose`, humorous-but-diagnostic network failures, defined exit codes, and CMake/Make install/update.

**Architecture:** Keep the existing C++ account client and the Go/OpenPGP F2F implementation. Build the Go package as a `c-archive` and call it from C++ through a small bridge. The console is a thin REPL over the existing `dispatch` path; named links are stored in vault payload v2 and each link owns its own session material. `status` reuses the existing PK engine and view model; `add dose` reuses the existing add mutation.

**Tech Stack:** C11, C++17, CMake >= 3.16, OpenSSL >= 3.0, libcurl, nlohmann/json, Rust printer, Go 1.23 (c-archive).

**Spec:** `docs/superpowers/specs/2026-10-21-hrt-tool-console-merge-design.md`

## Global Constraints

- C11/C++17; CMake >= 3.16; OpenSSL >= 3.0; libcurl; nlohmann/json; Rust stable; Go 1.23.
- Vault: AES-256-GCM, PBKDF2-SHA256 600000 iterations, mode `0600`, atomic replace, header as AAD.
- Only `socks5h` semantics: libcurl uses `CURLPROXY_SOCKS5_HOSTNAME`; the raw client sends the destination in the SOCKS5 domain-name field. `socks5://` is rewritten to `socks5h://`; HTTP(S)/SOCKS4 proxies are refused.
- Environment proxies are ignored; `no_proxy` cannot bypass an explicitly configured proxy.
- No `system`, `popen`, `exec`, `dlopen`, or shell. The Go bridge is a direct function call.
- Plaintext is written only to stdout; logs are encrypted; secrets never appear in `argv`, env, exceptions, logs, or `link show`/`doctor`.
- Exit codes `0`–`14` exactly as in spec §6.12; detail codes stay stable.
- Console strings only `en`/`ru`; no Chinese strings.
- `f2f` is a raw pass-through boundary and never inherits link server/proxy/mode/token.
- `--debug`/`--trace`: no humour, no retry, no progress animation; raw diagnostics and the technical error line only.
- Mutations never auto-retry. Reads may retry a tunnel interruption twice (0.5 s, 1.5 s).
- `make update` rebuilds and reinstalls using the cached prefix; no network fetch and no reconfigure.
- Work on a feature branch, not `main`. Commit after every task. Keep CI green.

## Review Focus

These are the five failure modes most likely to bite a user; each gets a test in the task that owns the code.

1. **Hidden-service DNS leak.** A `.onion`/`.i2p` name with `socks5://`, a missing proxy, or `no_proxy` set must fail before any local DNS lookup. Test: Task 2 (proxy normalisation) and Task 8 (raw client classification).
2. **Link credential cross-talk.** Switching from link A to link B must never send A's token to B's origin. Test: Task 5 (`select_link` + `SERVER_MISMATCH`).
3. **`--debug` still retries or jokes.** A debug connection failure must print raw diagnostics only, retry zero times, and still exit with the documented code. Test: Task 8.
4. **F2F flag/link bleed.** `hrt-tool f2f login --server ... --token-file ...` must reach Go with its flags intact and must not read the vault link. Test: Task 3 (parsing) and Task 13 (bridge).
5. **False success / duplicate dose.** A reset after a write must not retry, must not print `100% done`, and must tell the user to check `dose list`. Test: Task 11.

---

### Task 0: Branch and Commit the Approved Design/Plan

**Files:**
- Create: `docs/superpowers/specs/2026-10-21-hrt-tool-console-merge-design.md` (already written)
- Create: `docs/superpowers/plans/2026-10-21-hrt-tool-console-merge.md` (this file)

- [ ] **Step 1: Create the feature branch from the current `main`**

```bash
git switch -c feat/hrt-tool-console
```

- [ ] **Step 2: Commit the design and plan**

```bash
git add docs/superpowers/specs/2026-10-21-hrt-tool-console-merge-design.md docs/superpowers/plans/2026-10-21-hrt-tool-console-merge.md
git commit -m "docs: add the hrt-tool console and merge design"
```

- [ ] **Step 3: Run the baseline suite before changing code**

Run: `cmake --build tools/hrt-tool/build --parallel && ctest --test-dir tools/hrt-tool/build --output-on-failure`
Expected: the existing suite passes. If the build directory is stale, configure first with `cmake -S tools/hrt-tool -B tools/hrt-tool/build`.

---

### Task 1: Exit Codes and Error Classification

**Files:**
- Modify: `tools/hrt-tool/src/error.h`
- Modify: `tools/hrt-tool/src/api.h`
- Modify: `tools/hrt-tool/src/cli.cpp`
- Create: `tools/hrt-tool/src/exit_codes.cpp`
- Create: `tools/hrt-tool/tests/exit_code_test.cpp`
- Modify: `tools/hrt-tool/CMakeLists.txt`

**Interfaces:**
- Consumes: existing `ToolError`, `VaultAuthError`, `VaultWeakPasswordError`, `VaultCorruptError`, `ToolLimitError`.
- Produces:
  ```cpp
  enum class ExitCode : int {
      Ok = 0, Internal = 1, Usage = 2, Vault = 3, Network = 4, Data = 5,
      NotImplemented = 6, Auth = 7, Trust = 8, Permission = 9,
      Conflict = 10, Cancelled = 11, Config = 12, F2F = 13, Platform = 14,
  };
  const char *exit_code_name(ExitCode code);
  const char *exit_code_description(ExitCode code);
  ```

- [ ] **Step 1: Write the failing test**

```cpp
#include "api.h"
#include "error.h"
#include "test_framework.h"

int main() {
    CHECK_EQ(static_cast<int>(ExitCode::Internal), 1);
    CHECK_EQ(static_cast<int>(ExitCode::Auth), 7);
    CHECK_EQ(static_cast<int>(ExitCode::Trust), 8);
    CHECK_EQ(static_cast<int>(ExitCode::Permission), 9);
    CHECK_EQ(static_cast<int>(ExitCode::Conflict), 10);
    CHECK_EQ(static_cast<int>(ExitCode::Cancelled), 11);
    CHECK_EQ(static_cast<int>(ExitCode::Config), 12);
    CHECK_EQ(static_cast<int>(ExitCode::F2F), 13);
    CHECK_EQ(static_cast<int>(ExitCode::Platform), 14);
    CHECK_EQ(std::string(exit_code_name(ExitCode::Auth)), std::string("AUTH"));
    CHECK(ApiAuthError("NOT_LOGGED_IN", "x").exit_code() == ExitCode::Auth);
    CHECK(ApiTwoFactorRequired("totp").exit_code() == ExitCode::Auth);
    CHECK(SnapshotConflictError("id", 1).exit_code() == ExitCode::Conflict);
    return test_result();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build tools/hrt-tool/build --target exit_code_test && tools/hrt-tool/build/exit_code_test`
Expected: compile error, `ExitCode::Auth`/`exit_code_name` not declared.

- [ ] **Step 3: Implement the enum and names**

Add the enum to `error.h`. Implement `exit_code_name`/`exit_code_description` in `exit_codes.cpp` with the exact names and descriptions from spec §6.12. In `api.h`, change `ApiAuthError` and `ApiTwoFactorRequired` to `ExitCode::Auth`; change `SnapshotConflictError` to `ExitCode::Conflict`. In `cli.cpp`, the `std::exception` catch returns `ExitCode::Internal` (1) instead of `1` literal.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build tools/hrt-tool/build --target exit_code_test && tools/hrt-tool/build/exit_code_test`
Expected: `PASS`.

- [ ] **Step 5: Add the test to CMake and run the suite**

Add `src/exit_codes.cpp` to the `hrtcpp` source list, then add `exit_code_test` with `target_link_libraries(exit_code_test PRIVATE hrtcpp)` and `add_test(NAME exit_code COMMAND exit_code_test)`.
Run: `ctest --test-dir tools/hrt-tool/build --output-on-failure`
Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add tools/hrt-tool/src/error.h tools/hrt-tool/src/exit_codes.cpp tools/hrt-tool/src/api.h tools/hrt-tool/src/cli.cpp tools/hrt-tool/tests/exit_code_test.cpp tools/hrt-tool/CMakeLists.txt
git commit -m "feat(hrt-tool): define stable exit codes"
```

---

### Task 2: Proxy Normalisation and DNS-at-Proxy Policy

**Files:**
- Modify: `tools/hrt-tool/src/http.h`
- Modify: `tools/hrt-tool/src/http.cpp`
- Modify: `tools/hrt-tool/src/commands.cpp` (`http_options_for`, `command_config` proxy set)
- Create: `tools/hrt-tool/tests/proxy_test.cpp`
- Modify: `tools/hrt-tool/CMakeLists.txt`

**Interfaces:**
- Consumes: `Url parse_url(const std::string&)`, existing `validate_proxy_url`.
- Produces:
  ```cpp
  /* Returns the normalised proxy. socks5:// becomes socks5h://.
     Writes a one-line note to *note when the scheme changed. */
  std::string normalize_proxy_url(const std::string &proxy, std::ostream *note);
  ```
  `normalize_proxy_url` throws `ToolError(ExitCode::Usage, "PROXY_URL", ...)` for anything except `socks5`/`socks5h`.

- [ ] **Step 1: Write the failing test**

```cpp
#include "http.h"
#include "test_framework.h"

#include <sstream>

int main() {
    std::ostringstream note;
    CHECK_EQ(normalize_proxy_url("socks5://127.0.0.1:9050/", &note),
             std::string("socks5h://127.0.0.1:9050"));
    CHECK(note.str().find("socks5h") != std::string::npos);
    CHECK_EQ(normalize_proxy_url("socks5h://127.0.0.1:4447", nullptr),
             std::string("socks5h://127.0.0.1:4447"));
    CHECK_THROWS(normalize_proxy_url("http://127.0.0.1:8080", nullptr), ToolError);
    CHECK_THROWS(normalize_proxy_url("socks4://127.0.0.1:9050", nullptr), ToolError);
    CHECK_EQ(normalize_proxy_url("socks5://127.0.0.1", nullptr),
             std::string("socks5h://127.0.0.1:1080"));
    return test_result();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build tools/hrt-tool/build --target proxy_test && tools/hrt-tool/build/proxy_test`
Expected: compile error, `normalize_proxy_url` not declared.

- [ ] **Step 3: Implement `normalize_proxy_url`**

In `http.cpp`, parse with `parse_url`, accept `socks5`/`socks5h` case-insensitively, set the scheme to `socks5h`, default the port to `1080`, and rebuild `socks5h://host:port` (no path/userinfo). Reject everything else. Call it from `http_options_for` when `parsed.options.proxy_explicit`, and from `config set proxy` before storing. Keep `validate_proxy_url` as the strict post-normalisation check.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build tools/hrt-tool/build --target proxy_test && tools/hrt-tool/build/proxy_test`
Expected: `PASS`.

- [ ] **Step 5: Add the test and run the suite**

Add `proxy_test` to CMake. Run `ctest --test-dir tools/hrt-tool/build --output-on-failure`. Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add tools/hrt-tool/src/http.h tools/hrt-tool/src/http.cpp tools/hrt-tool/src/commands.cpp tools/hrt-tool/tests/proxy_test.cpp tools/hrt-tool/CMakeLists.txt
git commit -m "feat(hrt-tool): normalise socks5 proxies to socks5h"
```

---

### Task 3: CLI Parsing — Globals Anywhere and F2F Pass-Through

**Files:**
- Modify: `tools/hrt-tool/src/cli.h`
- Modify: `tools/hrt-tool/src/cli.cpp`
- Create: `tools/hrt-tool/tests/parse_args_test.cpp`
- Modify: `tools/hrt-tool/CMakeLists.txt`

**Interfaces:**
- Consumes: `ParsedArgs`, `GlobalOptions`.
- Produces:
  ```cpp
  ParsedArgs parse_cli_args(int argc, const char *const *argv);
  ```
  Behaviour:
  - Known global flags are parsed anywhere (`--debug`, `--trace`, `--json`, `--yes`, `--force`, `--utc`, `--server`, `--proxy`, `--config-dir`, `--token-file`, `--timeout`, `--trust-server`, `--insecure-tls`, `--transport`, `--compute`, `--vault-password-file`, `--vault-password-stdin`, `--log-file`, `--user-id`, `--account-password-file`, `--account-password-stdin`, `--snapshot`, `--mode`, `--device-name`, `--require-hrt`, `--no-cache-cloud-key`, `--no-color`).
  - Once the command token is `f2f`, every remaining token goes to `command_args` verbatim; no global parsing after it.
  - `-h`/`--help` after `f2f` stays in `command_args`.
  - Add `bool force = false;` and `bool probe = false;` to `GlobalOptions`.
  - Add `--probe` to the global set (used by `add-link`).

- [ ] **Step 1: Write the failing test**

```cpp
#include "cli.h"
#include "test_framework.h"

static ParsedArgs parse(std::vector<const char *> args) {
    args.insert(args.begin(), "hrt-tool");
    return parse_cli_args(static_cast<int>(args.size()), args.data());
}

int main() {
    ParsedArgs a = parse({"add", "dose", "--debug"});
    CHECK_EQ(a.command, std::string("add"));
    CHECK_EQ(a.command_args.size(), size_t{1});
    CHECK_EQ(a.command_args[0], std::string("dose"));
    CHECK(a.options.debug);

    ParsedArgs b = parse({"status", "--json"});
    CHECK(b.options.json);
    CHECK_EQ(b.command, std::string("status"));

    ParsedArgs c = parse({"f2f", "login", "--server", "https://x",
                          "--token-file", "t", "--help"});
    CHECK_EQ(c.command, std::string("f2f"));
    CHECK_EQ(c.command_args.size(), size_t{6});
    CHECK_EQ(c.command_args[0], std::string("login"));
    CHECK_EQ(c.command_args[1], std::string("--server"));
    CHECK_EQ(c.command_args[2], std::string("https://x"));
    CHECK_EQ(c.command_args[5], std::string("--help"));
    CHECK(!c.options.server_explicit);
    CHECK(!c.help);
    return test_result();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build tools/hrt-tool/build --target parse_args_test && tools/hrt-tool/build/parse_args_test`
Expected: compile error, `parse_cli_args` not declared.

- [ ] **Step 3: Implement the parser change**

Move the static `parse_args` into the `hrt` namespace or leave it file-static and wrap it:
```cpp
ParsedArgs parse_cli_args(int argc, const char *const *argv) { return parse_args(argc, argv); }
```
Add `#include "commands.h"` to `cli.h` so `ParsedArgs` is visible to callers and tests.
In `parse_args`, track `command_seen`; when the command token is `f2f`, set `raw = true`; if `raw`, push every token (including `-`-prefixed) to `command_args` and continue. Otherwise, for `-` tokens, check the global set first; unknown global-looking tokens after the command go to `command_args` as today.

Because `--force` becomes a global flag, replace the existing `has_argument(parsed.command_args, "--force")` checks in `snapshot pull`/`push`/`export`/`import` with `parsed.options.force`. F2F keeps its own `--force` because it is after the pass-through boundary.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build tools/hrt-tool/build --target parse_args_test && tools/hrt-tool/build/parse_args_test`
Expected: `PASS`.

- [ ] **Step 5: Add the test and run the suite**

Add `parse_args_test` to CMake. Run `ctest --test-dir tools/hrt-tool/build --output-on-failure`. Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add tools/hrt-tool/src/cli.h tools/hrt-tool/src/cli.cpp tools/hrt-tool/tests/parse_args_test.cpp tools/hrt-tool/CMakeLists.txt
git commit -m "feat(hrt-tool): parse global flags anywhere and pass f2f args through"
```

---

### Task 4: Vault Payload v2 — Links, Language, Migration

**Files:**
- Modify: `tools/hrt-tool/src/vault.h`
- Modify: `tools/hrt-tool/src/vault.cpp`
- Create: `tools/hrt-tool/tests/vault_links_test.cpp`
- Modify: `tools/hrt-tool/CMakeLists.txt`

**Interfaces:**
- Consumes: existing AES-GCM vault envelope, `nlohmann::json`.
- Produces:
  ```cpp
  struct LinkData {
      std::string url;
      std::string proxy;
      std::string mode;
      std::string token;
      std::string user_id;
      std::string username;
      std::string device_pass;
      std::string cloud_key;
      std::string device_key_pem;
      std::string device_cert_pem;
      std::string device_fingerprint;
      int64_t last_login_failed_at = 0;
  };

  struct VaultData {
      int version = 2;
      std::string language = "en";
      std::string welcome;
      bool humor = true;
      std::string active_link = "";
      std::map<std::string, LinkData> links;
      std::string mode;  // active-link view
      /* existing fields: token, user_id, username, device_pass, cloud_key,
         server, proxy, trust, device_*, opsec_ack, last_login_failed_at,
         created_at, updated_at */
  };

  nlohmann::json vault_data_to_json(const VaultData &data);
  VaultData vault_data_from_json(const nlohmann::json &value);
  ```

  `vault_data_from_json` accepts payload version `1` or `2`. For `1`, it creates `links[""]` from the top-level fields, sets `active_link = ""`, and returns `version = 2`. For `2`, it reads `links` and overlays `links[active_link]` onto the top-level view. `vault_data_to_json` first copies the top-level view into `links[active_link]`, then writes version `2`.

- [ ] **Step 1: Write the failing test**

```cpp
#include "vault.h"
#include "test_framework.h"

int main() {
    nlohmann::json v1 = {
        {"version", 1}, {"token", "tok"}, {"user_id", "u1"},
        {"username", "alice"}, {"server", "https://a.example"},
        {"proxy", ""}, {"trust", nlohmann::json::object()},
    };
    VaultData d = vault_data_from_json(v1);
    CHECK_EQ(d.version, 2);
    CHECK_EQ(d.active_link, std::string(""));
    CHECK(d.links.count("") == 1);
    CHECK_EQ(d.links[""].token, std::string("tok"));
    CHECK_EQ(d.links[""].url, std::string("https://a.example"));
    CHECK_EQ(d.token, std::string("tok"));

    d.links["mtf"] = LinkData{};
    d.links["mtf"].url = "hrt://b.example:9443";
    d.links["mtf"].token = "tok-b";
    d.links["mtf"].mode = "transfem";
    d.active_link = "mtf";
    d.token = "tok-b";
    d.server = "hrt://b.example:9443";
    d.mode = "transfem";
    nlohmann::json out = vault_data_to_json(d);
    CHECK_EQ(out.at("version").get<int>(), 2);
    VaultData back = vault_data_from_json(out);
    CHECK_EQ(back.active_link, std::string("mtf"));
    CHECK_EQ(back.token, std::string("tok-b"));
    CHECK_EQ(back.links.size(), size_t{2});
    CHECK_EQ(back.links[""].token, std::string("tok"));
    return test_result();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build tools/hrt-tool/build --target vault_links_test && tools/hrt-tool/build/vault_links_test`
Expected: compile error, `LinkData`/`vault_data_from_json` not declared.

- [ ] **Step 3: Implement the schema and migration**

Add `LinkData` and the new fields directly to `vault.h` (not a separate header; `link.h` in Task 5 will include `vault.h`, so a `LinkData` in `link.h` would create a circular include). Move `to_json`/`from_json` bodies into the exposed functions. `Vault::create` initializes `links[""]` with the default server (`https://hrt.mahiro.uk`) and `active_link = ""` before saving. In `vault_data_from_json`, if `active_link` is non-empty and missing from `links`, or if a v2 payload has no `links` object, throw `VaultCorruptError` rather than silently falling back to the base link. `Vault::create`, `Vault::unlock`, and `Vault::save` call the exposed functions. Keep the encrypted envelope unchanged. `Vault::unlock` must reject payload versions other than 1/2 with `VaultCorruptError`. Update existing tests/fixtures that assert the *payload* version is `1`; the encrypted envelope version stays `1`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build tools/hrt-tool/build --target vault_links_test && tools/hrt-tool/build/vault_links_test`
Expected: `PASS`.

- [ ] **Step 5: Add the test and run the suite**

Add `vault_links_test` to CMake. Run `ctest --test-dir tools/hrt-tool/build --output-on-failure`. Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add tools/hrt-tool/src/vault.h tools/hrt-tool/src/vault.cpp tools/hrt-tool/tests/vault_links_test.cpp tools/hrt-tool/CMakeLists.txt
git commit -m "feat(hrt-tool): add vault payload v2 links and migration"
```

---

### Task 5: Link Model, `add-link`, `commit-server`, Config Keys

**Files:**
- Create: `tools/hrt-tool/src/command_flags.h`
- Create: `tools/hrt-tool/src/command_flags.cpp`
- Create: `tools/hrt-tool/src/link.h`
- Create: `tools/hrt-tool/src/link.cpp`
- Modify: `tools/hrt-tool/src/http.h`
- Modify: `tools/hrt-tool/src/http.cpp`
- Modify: `tools/hrt-tool/src/commands.h`
- Modify: `tools/hrt-tool/src/commands.cpp`
- Create: `tools/hrt-tool/tests/link_test.cpp`
- Modify: `tools/hrt-tool/CMakeLists.txt`

**Interfaces:**
- Consumes: `VaultData`, `LinkData`, `parse_url`, `validate_server_url`, `normalize_proxy_url`.
- Produces in `command_flags.h`:
  ```cpp
  struct CommandFlags {
      std::map<std::string, std::string> values;
      std::set<std::string> flags;
      std::vector<std::string> positionals;
      std::vector<std::string> extras;
  };
  CommandFlags parse_command_flags(const std::vector<std::string> &args,
                                   const std::set<std::string> &value_flags,
                                   const std::set<std::string> &boolean_flags);
  ```
- Produces in `link.h` (which includes `vault.h`; `LinkData` lives in `vault.h`):
  ```cpp
  LinkData *find_link(VaultData &data, const std::string &name);
  const LinkData *find_link(const VaultData &data, const std::string &name);
  LinkData &require_link(VaultData &data, const std::string &name); // throws Config, "LINK_NOT_FOUND"
  void save_active_view(VaultData &data);  // view -> links[active_link]
  void load_active_view(VaultData &data);  // links[active_link] -> view
  void select_link(VaultData &data, const std::string &name);
  void add_link(VaultData &data, const std::string &name, const LinkData &link, bool force);
  void remove_link(VaultData &data, const std::string &name, bool force);
  void rename_link(VaultData &data, const std::string &old_name, const std::string &new_name);
  std::string link_summary(const std::string &name, const LinkData &link); // redacted
  ```
  Produces in `http.h`:
  ```cpp
  void validate_hidden_service_host(const Url &url); // moved from commands.cpp
  void validate_server_target(const std::string &server); // scheme/host policy
  ```

- [ ] **Step 1: Write the failing test**

```cpp
#include "link.h"
#include "vault.h"
#include "test_framework.h"

int main() {
    VaultData d;
    d.links[""] = LinkData{};
    d.links[""].url = "https://a.example";
    d.links[""].token = "tok-a";
    d.active_link = "";
    d.server = "https://a.example";
    d.token = "tok-a";

    LinkData mtf;
    mtf.url = "hrt://b.example:9443";
    mtf.proxy = "socks5h://127.0.0.1:9050";
    mtf.mode = "transfem";
    add_link(d, "mtf", mtf, false);
    CHECK(d.links.count("mtf") == 1);
    CHECK_THROWS(add_link(d, "mtf", mtf, false), ToolError);

    select_link(d, "mtf");
    CHECK_EQ(d.active_link, std::string("mtf"));
    CHECK_EQ(d.server, std::string("hrt://b.example:9443"));
    CHECK_EQ(d.mode, std::string("transfem"));
    CHECK_EQ(d.token, std::string(""));
    CHECK_EQ(d.links[""].token, std::string("tok-a"));

    select_link(d, "");
    CHECK_EQ(d.token, std::string("tok-a"));
    CHECK_EQ(d.links["mtf"].mode, std::string("transfem"));

    rename_link(d, "mtf", "main");
    CHECK(d.links.count("main") == 1);
    remove_link(d, "main", false);
    CHECK(d.links.count("main") == 0);
    CHECK_THROWS(remove_link(d, "", false), ToolError);

    LinkData secret = d.links[""];
    secret.token = "tok-b";
    secret.device_key_pem = "PRIVATE-KEY";
    const std::string summary = link_summary("base", secret);
    CHECK(summary.find("tok-b") == std::string::npos);
    CHECK(summary.find("PRIVATE-KEY") == std::string::npos);
    CHECK(summary.find("token: set") != std::string::npos);
    return test_result();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build tools/hrt-tool/build --target link_test && tools/hrt-tool/build/link_test`
Expected: compile error, link functions not declared.

- [ ] **Step 3: Implement link switching and commands**

Move `CommandFlags` and `parse_command_flags` from `local_commands.cpp` into `command_flags.cpp`; include the new header from `local_commands.cpp` and `commands.cpp` so `add-link` and `link` can parse their flags without duplicating the parser.

Implement `save_active_view`/`load_active_view` by copying the session fields (`url`↔`server`, `proxy`, `mode`, `token`, `user_id`, `username`, `device_pass`, `cloud_key`, `device_*`, `last_login_failed_at`). `select_link` saves the current view, sets `active_link`, loads the target, and updates `updated_at`. `remove_link` refuses `""` and the active link unless `force`; `force` first selects `""`.

In `commands.cpp`, add dispatch for:
- `add-link URL NAME [--mode M] [--proxy P] [--force] [--probe]`
- `commit-server NAME` (`-`/`""` selects base)
- `link list|show|rename|remove`
- `config get|set|unset` keys `language`, `welcome`, `humor`, `mode`, `active-link` in addition to `server`, `proxy`, `transport`, `compute`.

`add-link` uses `validate_server_target`, `normalize_proxy_url`, and stores a `LinkData` with empty secrets. `--probe` runs the existing trust probe via `ensure_trust`; without it, no network. `link show` prints only `token: set/empty`, `device key: present/absent`, never values. `link remove` requires `--yes` or a typed `yes`.

Move `validate_hidden_service_host` from `commands.cpp` to `http.cpp`; add `validate_server_target` that accepts `hrt`, `https`, and policy-limited `http`, and applies the onion/I2P rules.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build tools/hrt-tool/build --target link_test && tools/hrt-tool/build/link_test`
Expected: `PASS`.

- [ ] **Step 5: Add the test and run the suite**

Add `src/command_flags.cpp` and `src/link.cpp` to the `hrtcpp` source list and `link_test` to CMake. Run `ctest --test-dir tools/hrt-tool/build --output-on-failure`. Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add tools/hrt-tool/src/command_flags.h tools/hrt-tool/src/command_flags.cpp tools/hrt-tool/src/link.h tools/hrt-tool/src/link.cpp tools/hrt-tool/src/http.h tools/hrt-tool/src/http.cpp tools/hrt-tool/src/commands.h tools/hrt-tool/src/commands.cpp tools/hrt-tool/tests/link_test.cpp tools/hrt-tool/CMakeLists.txt
git commit -m "feat(hrt-tool): add named links and link commands"
```

---

### Task 6: SessionContext and Single-Unlock Refactor

**Files:**
- Create: `tools/hrt-tool/src/session.h`
- Create: `tools/hrt-tool/src/session.cpp`
- Modify: `tools/hrt-tool/src/commands.h`
- Modify: `tools/hrt-tool/src/commands.cpp`
- Modify: `tools/hrt-tool/src/cli.cpp`
- Create: `tools/hrt-tool/tests/session_test.cpp`
- Modify: `tools/hrt-tool/CMakeLists.txt`

**Interfaces:**
- Consumes: `Vault`, `VaultSession`, `read_vault_password`.
- Produces:
  ```cpp
  struct SessionContext {
      VaultSession session;
      std::string master;
      bool loaded = false;
      std::function<void(const std::string &)> progress; // may be empty
  };

  class UnlockedVault {
  public:
      VaultSession &session();
      const VaultSession &session() const;
      std::string &master();
      VaultLock *lock();
      // owner-or-view; non-copyable, movable
  };

  UnlockedVault unlock_full(const GlobalOptions &options, std::istream &in,
                            std::ostream &out, SessionContext *context);
  UnlockedVault unlock_required(const GlobalOptions &options, std::istream &in,
                                std::ostream &out, SessionContext *context);
  int dispatch(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
               std::ostream &err, SessionContext *context = nullptr);
  ```

- [ ] **Step 1: Write the failing test**

```cpp
#include "commands.h"
#include "session.h"
#include "vault.h"
#include "test_framework.h"

#include <filesystem>
#include <iostream>
#include <sstream>

int main() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "hrt-session-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path vault = dir / "vault.hrtv";
    Vault::create(vault, "correct horse battery staple");

    GlobalOptions options;
    options.config_dir = dir.string();
    SessionContext context;
    std::istringstream first("correct horse battery staple\n");
    {
        UnlockedVault u = unlock_full(options, first, std::cout, &context);
        CHECK(context.loaded);
        CHECK_EQ(u.session().data.version, 2);
        u.session().data.language = "ru";
    }
    std::istringstream second("");
    std::ostringstream prompt;
    {
        UnlockedVault u = unlock_full(options, second, prompt, &context);
        CHECK_EQ(u.session().data.language, std::string("ru"));
        CHECK(prompt.str().find("password") == std::string::npos);
    }
    std::filesystem::remove_all(dir);
    return test_result();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build tools/hrt-tool/build --target session_test && tools/hrt-tool/build/session_test`
Expected: compile error, `SessionContext`/`unlock_full` signature missing.

- [ ] **Step 3: Extract the session code**

Move `VaultLock` and `UnlockedVault` from `commands.cpp` into `session.cpp`. Implement the owner-or-view storage with pointers:
```cpp
VaultSession *session_ = nullptr;
std::string *master_ = nullptr;
std::optional<VaultSession> owned_session_;
std::string owned_master_;
```
`unlock_full` uses `context->session`/`context->master` when `context->loaded`; otherwise unlocks and, if `context` is non-null, moves the session/master into it and points at it. `unlock_required` does the same without the reset path.

Update every `unlocked.session.data` to `unlocked.session().data` and `unlocked.master` to `unlocked.master()` in `commands.cpp`. Update `dispatch` calls in `cli.cpp` to pass `nullptr`. Add the default argument so existing tests compile.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build tools/hrt-tool/build --target session_test && tools/hrt-tool/build/session_test`
Expected: `PASS`.

- [ ] **Step 5: Add the source and test to CMake, then run the full suite**

Add `src/session.cpp` to the `hrtcpp` source list and `session_test` to CMake.
Run: `ctest --test-dir tools/hrt-tool/build --output-on-failure`
Expected: all pass; no command re-prompts the vault.

- [ ] **Step 6: Commit**

```bash
git add tools/hrt-tool/src/session.h tools/hrt-tool/src/session.cpp tools/hrt-tool/src/commands.h tools/hrt-tool/src/commands.cpp tools/hrt-tool/src/cli.cpp tools/hrt-tool/tests/session_test.cpp tools/hrt-tool/CMakeLists.txt
git commit -m "refactor(hrt-tool): unlock the vault once per console session"
```

---

### Task 7: Console i18n (English/Russian)

**Files:**
- Create: `tools/hrt-tool/src/i18n.h`
- Create: `tools/hrt-tool/src/i18n.cpp`
- Create: `tools/hrt-tool/tests/i18n_test.cpp`
- Modify: `tools/hrt-tool/CMakeLists.txt`

**Interfaces:**
- Consumes: `VaultData::language`.
- Produces:
  ```cpp
  enum class Language { En, Ru };
  Language parse_language(const std::string &value); // en|ru, throws Config
  const char *language_name(Language language);      // "en"|"ru"
  enum class I18nKey {
      Welcome, HelpHeader, HelpFooter, PromptHelp, UnknownCommand,
      ExitHint, ConsoleInitRefused, TunnelInterrupted, ServerInitiated,
      Timeout, ProxyDown, FinalFallback,
  };
  std::string tr(Language language, I18nKey key);
  ```

- [ ] **Step 1: Write the failing test**

```cpp
#include "i18n.h"
#include "test_framework.h"

int main() {
    CHECK(tr(Language::En, I18nKey::Welcome).find("hrt-tool") != std::string::npos);
    CHECK(tr(Language::En, I18nKey::TunnelInterrupted).find("Tunnel dropped. Fuck!") != std::string::npos);
    CHECK(tr(Language::En, I18nKey::FinalFallback).find("Oh no, the other side") != std::string::npos);
    CHECK(tr(Language::Ru, I18nKey::Welcome).find("hrt-tool") != std::string::npos);
    CHECK(tr(Language::Ru, I18nKey::TunnelInterrupted).find("Туннель") != std::string::npos);
    CHECK_THROWS(parse_language("zh"), ToolError);
    for (I18nKey key : {I18nKey::Welcome, I18nKey::HelpHeader, I18nKey::TunnelInterrupted}) {
        const std::string text = tr(Language::En, key) + tr(Language::Ru, key);
        CHECK(text.find("的") == std::string::npos); // no CJK
    }
    return test_result();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build tools/hrt-tool/build --target i18n_test && tools/hrt-tool/build/i18n_test`
Expected: compile error, i18n not declared.

- [ ] **Step 3: Implement the string tables**

Use the exact en/ru wording from spec §6.10 for the five failure keys. Use the spec's welcome/help wording. Keep all strings ASCII/Cyrillic; no CJK. `parse_language` accepts `en`/`ru` only and throws `ToolError(ExitCode::Config, "CONFIG_VALUE", ...)`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build tools/hrt-tool/build --target i18n_test && tools/hrt-tool/build/i18n_test`
Expected: `PASS`.

- [ ] **Step 5: Add the test and run the suite**

Add `src/i18n.cpp` to the `hrtcpp` source list and `i18n_test` to CMake. Run `ctest --test-dir tools/hrt-tool/build --output-on-failure`. Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add tools/hrt-tool/src/i18n.h tools/hrt-tool/src/i18n.cpp tools/hrt-tool/tests/i18n_test.cpp tools/hrt-tool/CMakeLists.txt
git commit -m "feat(hrt-tool): add en/ru console strings"
```

---

### Task 8: Network Failure Classification, Humour, and Retry Rules

**Files:**
- Create: `tools/hrt-tool/src/net_error.h`
- Create: `tools/hrt-tool/src/net_error.cpp`
- Modify: `tools/hrt-tool/src/http.cpp`
- Modify: `tools/hrt-tool/src/hrt_client.cpp`
- Modify: `tools/hrt-tool/src/commands.cpp`
- Modify: `tools/hrt-tool/src/cli.cpp`
- Create: `tools/hrt-tool/tests/net_error_test.cpp`
- Modify: `tools/hrt-tool/CMakeLists.txt`

**Interfaces:**
- Consumes: `curl` result codes, `errno`, HTTP status, SOCKS5 replies, `i18n.h`.
- Produces:
  ```cpp
  enum class FailureKind {
      None, TunnelInterrupted, ServerInitiated, Timeout, ProxyDown, FinalFallback,
  };
  class NetworkFailure : public ToolError {
  public:
      NetworkFailure(std::string code, std::string message, FailureKind kind);
      FailureKind kind() const noexcept;
  };
  FailureKind classify_curl(int curl_result, int errnum);
  FailureKind classify_errno(int errnum);
  FailureKind classify_http(long status);
  FailureKind classify_socks5(unsigned char reply);
  bool should_retry(FailureKind kind, bool is_read, bool debug, int attempts);
  std::string failure_human_line(Language language, FailureKind kind);
  ```

- [ ] **Step 1: Write the failing test**

```cpp
#include "net_error.h"
#include "test_framework.h"

int main() {
    CHECK(classify_curl(CURLE_RECV_ERROR, ECONNRESET) == FailureKind::TunnelInterrupted);
    CHECK(classify_curl(CURLE_SEND_ERROR, EPIPE) == FailureKind::TunnelInterrupted);
    CHECK(classify_curl(CURLE_COULDNT_CONNECT, ECONNREFUSED) == FailureKind::ServerInitiated);
    CHECK(classify_curl(CURLE_OPERATION_TIMEDOUT, 0) == FailureKind::Timeout);
    CHECK(classify_http(502) == FailureKind::ServerInitiated);
    CHECK(classify_http(503) == FailureKind::ServerInitiated);
    CHECK(classify_http(500) == FailureKind::ServerInitiated);
    CHECK(classify_socks5(0x05) == FailureKind::ServerInitiated);
    CHECK(classify_socks5(0x04) == FailureKind::TunnelInterrupted);
    CHECK(should_retry(FailureKind::TunnelInterrupted, true, false, 0));
    CHECK(!should_retry(FailureKind::TunnelInterrupted, true, true, 0));
    CHECK(!should_retry(FailureKind::TunnelInterrupted, false, false, 0));
    CHECK(!should_retry(FailureKind::TunnelInterrupted, true, false, 2));
    CHECK(!should_retry(FailureKind::ServerInitiated, true, false, 0));
    CHECK(!should_retry(FailureKind::ProxyDown, true, false, 0));
    CHECK(failure_human_line(Language::En, FailureKind::FinalFallback)
              .find("cries") != std::string::npos);
    NetworkFailure nf("TUNNEL_INTERRUPTED", "reset", FailureKind::TunnelInterrupted);
    CHECK(nf.exit_code() == ExitCode::Network);
    return test_result();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build tools/hrt-tool/build --target net_error_test && tools/hrt-tool/build/net_error_test`
Expected: compile error, `FailureKind` not declared.

- [ ] **Step 3: Implement classification and integrate it**

Implement the mappings. In `http.cpp`, replace the final `throw ToolError(..., "HTTP", curl_easy_strerror(result))` with a `NetworkFailure` built from `classify_curl(result, errno)` and the original detail code. Keep `CURLE_SSL_PINNEDPUBKEYNOTMATCH` as `ToolError(ExitCode::Trust, "TRUST_CHANGED", ...)`. In `hrt_client.cpp`, wrap `HRT_CONNECT`, `HRT_PROXY`, DNS, and read/write failures in `NetworkFailure`. Keep the existing message text and code strings. Reclassify `TRUST_NO_PIN`, `TRUST_CHANGED`, `TRUST_REFUSED`, `SERVER_MISMATCH`, `PROXY_DNS`, and `HRT_TRUST` to `ExitCode::Trust`.

In `cli.cpp`, catch `NetworkFailure` before `ToolError`: print the human line unless `--debug`/`--trace`/`--no-humor`/`humor=false`, then the technical line and `[CODE]`, and return the exit code. One-shot commands use `en` for the human line (the vault may not be unlocked when the failure is printed); the console passes its configured `Language` so its lines are localised.

Add a read-retry loop in `command_look`/the live snapshot GET: on `NetworkFailure` with `should_retry(kind, true, debug, attempt)`, print `tr(lang, Timeout)` and sleep 500 ms/1500 ms. Do not add retries to mutations. After the last attempt, throw a `NetworkFailure` with `FinalFallback` so the top-level printer emits the fallback line.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build tools/hrt-tool/build --target net_error_test && tools/hrt-tool/build/net_error_test`
Expected: `PASS`.

- [ ] **Step 5: Add the test and run the suite**

Add `src/net_error.cpp` to the `hrtcpp` source list and `net_error_test` to CMake. Run `ctest --test-dir tools/hrt-tool/build --output-on-failure`. Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add tools/hrt-tool/src/net_error.h tools/hrt-tool/src/net_error.cpp tools/hrt-tool/src/http.cpp tools/hrt-tool/src/hrt_client.cpp tools/hrt-tool/src/commands.cpp tools/hrt-tool/src/cli.cpp tools/hrt-tool/tests/net_error_test.cpp tools/hrt-tool/CMakeLists.txt
git commit -m "feat(hrt-tool): classify network failures and honour debug mode"
```

---

### Task 9: Interactive Console REPL

**Files:**
- Create: `tools/hrt-tool/src/console.h`
- Create: `tools/hrt-tool/src/console.cpp`
- Modify: `tools/hrt-tool/src/commands.h`
- Modify: `tools/hrt-tool/src/commands.cpp`
- Modify: `tools/hrt-tool/src/cli.cpp`
- Create: `tools/hrt-tool/tests/console_test.cpp`
- Modify: `tools/hrt-tool/CMakeLists.txt`

**Interfaces:**
- Consumes: `parse_cli_args`, `dispatch`, `SessionContext`, `unlock_full`, `link.h`, `i18n.h`.
- Produces:
  ```cpp
  std::vector<std::string> tokenize_console_line(const std::string &line,
                                                 std::string *error);
  int run_console(const GlobalOptions &options, std::istream &in,
                  std::ostream &out, std::ostream &err);
  ```

  `run_console`:
  - Unlocks the vault once (prompts for the master password). If the vault
    is missing, prints `VAULT_MISSING` with the `hrt-tool init` command and
    returns exit code 3; the console never runs `init` itself.
  - Starts in the persisted `active_link` (prompt `hrt$<name>>`) or base (`hrt>`).
  - Reads lines, tokenises, and dispatches.
  - `exit` in a link selects the base link (persists it) and continues;
    `exit`/`quit` at base returns `0`; `Ctrl-D` returns `0` and keeps the
    current link.
  - `init`/`init --reset` returns `CONSOLE_INIT` without exiting.
  - `debug on|off` sets `GlobalOptions.debug` for the session.
  - `clear` emits ANSI clear only when `out` is a TTY and `--no-color` is
    not set.
  - Unknown commands print the error and continue.

- [ ] **Step 1: Write the failing test**

```cpp
#include "console.h"
#include "test_framework.h"

int main() {
    std::string error;
    auto tokens = tokenize_console_line("add dose --drug \"Estradiol valerate\"", &error);
    CHECK_EQ(tokens.size(), size_t{4});
    CHECK_EQ(tokens[3], std::string("Estradiol valerate"));
    CHECK(tokenize_console_line("add 'unterminated", &error).empty());
    CHECK(!error.empty());

    tokens = tokenize_console_line("proxy socks5://127.0.0.1:9050/ # tor", &error);
    CHECK_EQ(tokens.size(), size_t{2});
    CHECK_EQ(tokens[0], std::string("proxy"));
    CHECK_EQ(tokens[1], std::string("socks5://127.0.0.1:9050/"));
    return test_result();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build tools/hrt-tool/build --target console_test && tools/hrt-tool/build/console_test`
Expected: compile error, `tokenize_console_line` not declared.

- [ ] **Step 3: Implement the tokeniser and REPL**

Tokeniser: whitespace splits; `'...'` and `"..."` group; `\` escapes the next character; `#` starts a comment only at the start of a token; unmatched quote sets `*error` and returns an empty vector.

REPL: implement the state machine above. Intercept `commit-server` and `exit` in the console loop: call `select_link(context.session.data, name)`, save the vault through the context's master password, and update the prompt; do not just dispatch them as ordinary commands. For all other non-`f2f` commands, build `ParsedArgs` via `parse_cli_args` and override `server`, `proxy`, `mode`, and `config_dir` from the active link/console options. For `f2f`, pass `command_args` verbatim and do not apply link fields. Call `dispatch(parsed, in, out, err, &context)` inside a `try`/`catch` for `NetworkFailure`, `ToolError`, and `std::exception`: print the localised human line plus the technical line and continue the console (only `exit` leaves). At startup, print `context.session.data.welcome` when it is non-empty, otherwise `tr(language, Welcome)`; print the prompt before each read.

In `cli.cpp`, if `argc == 1` and `stdin_is_tty()`, call `run_console(GlobalOptions{}, in, out, err)`. Add a `console` command that calls `run_console` with the parsed globals.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build tools/hrt-tool/build --target console_test && tools/hrt-tool/build/console_test`
Expected: `PASS`.

- [ ] **Step 5: Add the test and run the suite**

Add `src/console.cpp` to the `hrtcpp` source list and `console_test` to CMake. Run `ctest --test-dir tools/hrt-tool/build --output-on-failure`. Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add tools/hrt-tool/src/console.h tools/hrt-tool/src/console.cpp tools/hrt-tool/src/commands.h tools/hrt-tool/src/commands.cpp tools/hrt-tool/src/cli.cpp tools/hrt-tool/tests/console_test.cpp tools/hrt-tool/CMakeLists.txt
git commit -m "feat(hrt-tool): add the interactive console"
```

---

### Task 10: Shared Formatting and the `status` Command

**Files:**
- Create: `tools/hrt-tool/src/format.h`
- Create: `tools/hrt-tool/src/format.cpp`
- Create: `tools/hrt-tool/src/status.h`
- Create: `tools/hrt-tool/src/status.cpp`
- Modify: `tools/hrt-tool/src/local_commands.h`
- Modify: `tools/hrt-tool/src/local_commands.cpp`
- Modify: `tools/hrt-tool/src/commands.cpp`
- Modify: `tools/hrt-tool/src/cli.cpp`
- Create: `tools/hrt-tool/tests/status_test.cpp`
- Modify: `tools/hrt-tool/CMakeLists.txt`

**Interfaces:**
- Consumes: `run_pk`, `class_value`, `parse_offsets`, `normalize_sync_payload`, `resolve_mode`, `hrtprint_render`.
- Produces in `format.h`:
  ```cpp
  std::string format_time(double hours, bool utc);        // existing look format
  std::string format_time_status(double hours, bool utc); // "UTC +0 1970-01-01 00:00"
  std::string format_duration(double hours);
  std::string format_elapsed(double hours); // "1d , 11h , 10min"
  std::string format_concentration(double value, int ester);
  std::string format_lab_value(double value, const std::string &unit);
  std::string format_lab_value_status(double value, const std::string &unit); // "a \ b"
  std::string format_assay(const std::string &assay); // "Mass spectrometry"|"Immunoassay"
  ```
  Produces in `local_commands.h`:
  ```cpp
  nlohmann::json build_pk_view(const ParsedArgs &parsed, const nlohmann::json &payload,
                               const std::vector<std::pair<std::string,double>> &offsets,
                               std::ostream &err);
  std::vector<std::pair<std::string,double>> parse_offsets(const CommandFlags &flags);
  ```
  `local_commands.h` includes `command_flags.h` for `CommandFlags`.
  Produces in `status.h`:
  ```cpp
  nlohmann::json build_status_view(const ParsedArgs &parsed, const nlohmann::json &payload,
                                   bool all, std::ostream &err);
  void render_status_table(const nlohmann::json &view, bool all, std::ostream &out);
  int handle_status(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                    std::ostream &err, SessionContext *context);
  int parse_watch_seconds(const std::string &text); // > 0, throws Usage
  ```

- [ ] **Step 1: Write the failing test**

```cpp
#include "format.h"
#include "status.h"
#include "test_framework.h"

#include <sstream>

int main() {
    CHECK_EQ(format_elapsed(24.0 + 11.0 + 10.0 / 60.0), std::string("1d , 11h , 10min"));
    CHECK_EQ(format_time_status(0.0, true), std::string("UTC +0 1970-01-01 00:00"));
    CHECK_EQ(format_time(0.0, true), std::string("1970-01-01 00:00 UTC"));
    CHECK_EQ(format_lab_value_status(169.0, "pg/ml"), std::string("169.0 pg/mL \\ 620.3 pmol/L"));
    CHECK_EQ(format_assay("lcms"), std::string("Mass spectrometry"));
    CHECK_EQ(parse_watch_seconds("5"), 5);
    CHECK_THROWS(parse_watch_seconds("0"), ToolError);
    CHECK_THROWS(parse_watch_seconds("abc"), ToolError);

    nlohmann::json payload = {
        {"modes", {{"transfem", {{"events", nlohmann::json::array()},
                                  {"labResults", nlohmann::json::array()}}}}},
        {"weight", 70.0},
    };
    const double now = 20000.0;
    payload["modes"]["transfem"]["events"].push_back({
        {"id", "e1"}, {"route", "injection"}, {"ester", "EV"},
        {"doseMG", 2.0}, {"timeH", now - 24.0}, {"extras", nlohmann::json::object()},
    });
    payload["modes"]["transfem"]["labResults"].push_back({
        {"id", "l1"}, {"timeH", now - 1.0}, {"concValue", 169.0},
        {"unit", "pg/ml"}, {"assay", "lcms"},
    });
    ParsedArgs parsed;
    parsed.options.utc = true;
    parsed.command_args = {"all"};
    nlohmann::json view = build_status_view(parsed, payload, true, std::cout);
    CHECK(view["rows"].is_array());
    CHECK(view["rows"].size() > 1);
    CHECK(view["rows"][0]["drug"] == "Estradiol valerate");
    std::ostringstream out;
    render_status_table(view, true, out);
    CHECK(out.str().find("Estradiol valerate\\2mg") != std::string::npos);
    CHECK(out.str().find("Expected") != std::string::npos);
    CHECK(out.str().find("Mass spectrometry") != std::string::npos);
    return test_result();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build tools/hrt-tool/build --target status_test && tools/hrt-tool/build/status_test`
Expected: compile error, `format_elapsed`/`build_status_view` not declared.

- [ ] **Step 3: Move formatting helpers and build the status view**

Move `format_time`, `format_duration`, `format_concentration`, and `format_lab_value` out of the anonymous namespace in `local_commands.cpp` into `format.cpp` with the same behaviour. Add `format_elapsed`, `format_lab_value_status`, and `format_assay`.

Refactor `render_look` into `build_pk_view` (returns the existing view object) plus a `render_view` call. `build_status_view` calls `build_pk_view` with the offsets from `--at` (through the existing `parse_offsets`) or the default list, then builds `rows`; `--labs N` caps the lab block:
```json
{"drug":"Estradiol valerate","dose":"2mg","lastDose":"1d , 11h , 10min",
 "now":"169.0 pg/mL","offset":"1min","expected":"165.0 pg/mL"}
```
Render the table in the issue's column order, then the lab block and `done`. Print the uncalibrated-baseline warning to `err`, not `out`, so the table pipes cleanly. `--json` emits the view object. `status --watch SECONDS` re-renders until Ctrl-C; reject `SECONDS <= 0`.

Wire `command_status` in `commands.cpp` using the same live/local branch as `look`; set `options.mode` from the active link in the live path when `--mode` is absent. Add `status` to help and dispatch.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build tools/hrt-tool/build --target status_test && tools/hrt-tool/build/status_test`
Expected: `PASS`.

- [ ] **Step 5: Add the test and run the suite**

Add `src/format.cpp` and `src/status.cpp` to the `hrtcpp` source list and `status_test` to CMake. Run `ctest --test-dir tools/hrt-tool/build --output-on-failure`. Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add tools/hrt-tool/src/format.h tools/hrt-tool/src/format.cpp tools/hrt-tool/src/status.h tools/hrt-tool/src/status.cpp tools/hrt-tool/src/local_commands.h tools/hrt-tool/src/local_commands.cpp tools/hrt-tool/src/commands.cpp tools/hrt-tool/src/cli.cpp tools/hrt-tool/tests/status_test.cpp tools/hrt-tool/CMakeLists.txt
git commit -m "feat(hrt-tool): add the status concentration view"
```

---

### Task 11: Interactive `add dose` and Progress

**Files:**
- Create: `tools/hrt-tool/src/add_interactive.h`
- Create: `tools/hrt-tool/src/add_interactive.cpp`
- Modify: `tools/hrt-tool/src/local_commands.cpp` (time parser, progress phase calls)
- Modify: `tools/hrt-tool/src/commands.cpp` (dispatch + live progress phases)
- Modify: `tools/hrt-tool/src/console.cpp` (`add dose` routing)
- Create: `tools/hrt-tool/tests/add_interactive_test.cpp`
- Modify: `tools/hrt-tool/CMakeLists.txt`

**Interfaces:**
- Consumes: `SessionContext::progress`, `apply_add`, `parse_ester`, `parse_route`, `parse_dose_mg`, `parse_time_hours`, `dispatch`.
- Produces:
  ```cpp
  bool prompt_add_dose(std::istream &in, std::ostream &out,
                       const GlobalOptions &options,
                       AddDoseAnswers *answers);
  int handle_add_interactive(const ParsedArgs &parsed, std::istream &in,
                             std::ostream &out, std::ostream &err,
                             SessionContext *context);
  double parse_time_hours(const std::string &text, bool utc); // exported from local_commands
  ```
  `AddDoseAnswers { std::string drug; std::string dose; std::string time; std::string route; }`.

- [ ] **Step 1: Write the failing test**

```cpp
#include "add_interactive.h"
#include "local_commands.h"
#include "test_framework.h"

#include <sstream>

int main() {
    CHECK_NEAR(parse_time_hours("2026,10,20:18:29", true),
               497922.4833333333, 1e-6); // 2026-10-20T18:29Z in hours
    AddDoseAnswers answers;
    std::istringstream in("Estradiol valerate\n2mg\n2026,10,20:18:29\ninjection\nyes\n");
    std::ostringstream out;
    GlobalOptions options;
    CHECK(prompt_add_dose(in, out, options, &answers));
    CHECK_EQ(answers.drug, std::string("Estradiol valerate"));
    CHECK_EQ(answers.dose, std::string("2mg"));
    CHECK_EQ(answers.route, std::string("injection"));
    CHECK(out.str().find("[Yes/No]") != std::string::npos);
    return test_result();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build tools/hrt-tool/build --target add_interactive_test && tools/hrt-tool/build/add_interactive_test`
Expected: compile error, `prompt_add_dose`/`parse_time_hours` not declared.

- [ ] **Step 3: Implement the interview and progress**

Extend `parse_time_hours` to accept `YYYY,M,D:HH:MM` and `YYYY,M,D` by translating commas to the existing `YYYY-MM-DD` path. Export `parse_time_hours` in `local_commands.h`.

`prompt_add_dose` prints the four prompts, accepts a drug name with an embedded dose (split on the last whitespace-number sequence), validates the dose and route, prints the summary line, and reads `yes`/`y` or `no`/`n`; `no` restarts with previous answers as defaults. It returns false on EOF or `no` after the first pass? Keep `no` looping; return false on EOF.

`handle_add_interactive` builds a `ParsedArgs` copy with `command_args = {"dose", "--drug", ..., "--time", ..., "--measurement", ..., "--route", ...}` and calls `dispatch` with the same `SessionContext`. Set `context->progress` to a callback that prints the phase and a TTY progress bar; print `Recording...` before dispatch. Call `dispatch` with a local `std::ostringstream add_out` so the record id line can be captured and reprinted. On success print the record id, `hrt:YES`, the timestamp/user block, and `100% done`; on failure print the failed phase and do not print `100%`.

In the live/local add paths, call `context->progress("trust")`, `"fetching snapshot"`, `"applying"`, `"uploading"` (or `"reading"`, `"applying"`, `"writing"` locally) at the phase boundaries. In debug mode, the progress callback prints phase lines only, no animation.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build tools/hrt-tool/build --target add_interactive_test && tools/hrt-tool/build/add_interactive_test`
Expected: `PASS`.

- [ ] **Step 5: Add the test and run the suite**

Add `src/add_interactive.cpp` to the `hrtcpp` source list and `add_interactive_test` to CMake. Run `ctest --test-dir tools/hrt-tool/build --output-on-failure`. Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add tools/hrt-tool/src/add_interactive.h tools/hrt-tool/src/add_interactive.cpp tools/hrt-tool/src/local_commands.cpp tools/hrt-tool/src/local_commands.h tools/hrt-tool/src/commands.cpp tools/hrt-tool/src/console.cpp tools/hrt-tool/tests/add_interactive_test.cpp tools/hrt-tool/CMakeLists.txt
git commit -m "feat(hrt-tool): add interactive dose recording"
```

---

### Task 12: Extra Commands — `proxy`, `doctor`, `completion`, `version --json`, `help`

**Files:**
- Create: `tools/hrt-tool/src/proxy_commands.cpp`
- Create: `tools/hrt-tool/src/doctor.cpp`
- Create: `tools/hrt-tool/src/completion.h`
- Create: `tools/hrt-tool/src/completion.cpp`
- Modify: `tools/hrt-tool/src/commands.h`
- Modify: `tools/hrt-tool/src/commands.cpp`
- Modify: `tools/hrt-tool/src/cli.cpp`
- Create: `tools/hrt-tool/tests/doctor_test.cpp`
- Create: `tools/hrt-tool/tests/completion_test.cpp`
- Create: `tools/hrt-tool/tests/proxy_commands_test.cpp`
- Create: `tools/hrt-tool/tests/version_json_test.cpp`
- Modify: `tools/hrt-tool/CMakeLists.txt`

**Interfaces:**
- Consumes: `SessionContext`, `VaultData`, `normalize_proxy_url`, `HttpClient`, `i18n.h`, `exit_codes.cpp`.
- Produces:
  ```cpp
  int command_proxy(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                    std::ostream &err, SessionContext *context);
  int command_doctor(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                     std::ostream &err, SessionContext *context);
  int command_completion(const ParsedArgs &parsed, std::ostream &out);
  int render_completion(const std::string &shell, std::ostream &out);
  ```

- [ ] **Step 1: Write the failing tests**

```cpp
// doctor_test.cpp
#include "commands.h"
#include "session.h"
#include "vault.h"
#include "test_framework.h"

#include <filesystem>
#include <fstream>
#include <sstream>

int main() {
    const auto dir = std::filesystem::temp_directory_path() / "hrt-doctor-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    Vault::create(dir / "vault.hrtv", "correct horse battery staple");
    std::ofstream(dir / "pass") << "correct horse battery staple\n";
    std::filesystem::permissions(dir / "pass",
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);

    GlobalOptions options;
    options.config_dir = dir.string();
    options.vault_password_file = (dir / "pass").string();
    SessionContext context;
    std::istringstream in;
    std::ostringstream out, err;
    UnlockedVault u = unlock_full(options, in, out, &context);
    u.session().data.token = "SECRET-TOKEN";

    ParsedArgs parsed;
    parsed.command = "doctor";
    CHECK_EQ(command_doctor(parsed, in, out, err, &context), 0);
    CHECK(out.str().find("PASS") != std::string::npos);
    CHECK(out.str().find("SECRET-TOKEN") == std::string::npos);
    CHECK(out.str().find("correct horse") == std::string::npos);
    std::filesystem::remove_all(dir);
    return test_result();
}
```

```cpp
// completion_test.cpp
#include "completion.h"
#include "test_framework.h"

#include <sstream>

int main() {
    std::ostringstream bash, zsh, fish;
    CHECK_EQ(render_completion("bash", bash), 0);
    CHECK_EQ(render_completion("zsh", zsh), 0);
    CHECK_EQ(render_completion("fish", fish), 0);
    CHECK(bash.str().find("hrt-tool") != std::string::npos);
    CHECK_THROWS(render_completion("tcsh", bash), ToolError);
    return test_result();
}
```

```cpp
// proxy_commands_test.cpp
#include "commands.h"
#include "session.h"
#include "vault.h"
#include "test_framework.h"

#include <filesystem>
#include <fstream>
#include <sstream>

int main() {
    const auto dir = std::filesystem::temp_directory_path() / "hrt-proxy-cmd-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    Vault::create(dir / "vault.hrtv", "correct horse battery staple");
    std::ofstream(dir / "pass") << "correct horse battery staple\n";
    std::filesystem::permissions(dir / "pass",
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);

    GlobalOptions options;
    options.config_dir = dir.string();
    options.vault_password_file = (dir / "pass").string();
    SessionContext context;
    std::istringstream in;
    std::ostringstream out, err;

    ParsedArgs tor;
    tor.command = "proxy";
    tor.command_args = {"tor"};
    CHECK_EQ(command_proxy(tor, in, out, err, &context), 0);
    CHECK_EQ(context.session.data.proxy, std::string("socks5h://127.0.0.1:9050"));

    ParsedArgs off;
    off.command = "proxy";
    off.command_args = {"off"};
    CHECK_EQ(command_proxy(off, in, out, err, &context), 0);
    CHECK(context.session.data.proxy.empty());
    std::filesystem::remove_all(dir);
    return test_result();
}
```

```cpp
// version_json_test.cpp
#include "cli.h"
#include "test_framework.h"

#include <nlohmann/json.hpp>
#include <sstream>

int main() {
    const char *argv[] = {"hrt-tool", "version", "--json"};
    std::istringstream in;
    std::ostringstream out, err;
    CHECK_EQ(run_cli(3, argv, in, out, err), 0);
    const nlohmann::json parsed = nlohmann::json::parse(out.str());
    CHECK(parsed.contains("tool"));
    return test_result();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build tools/hrt-tool/build --target completion_test && tools/hrt-tool/build/completion_test`
Expected: compile error, `render_completion` not declared.

- [ ] **Step 3: Implement the commands**

`proxy tor|i2p|off|status|test`:
- `tor`/`i2p` store `socks5h://127.0.0.1:9050` / `:4447`.
- `off` clears.
- `status` prints the normalised proxy and the inferred network.
- `test` connects to the proxy and requests `/api/tool/v1/version` through it with no token; report success/failure and whether the target was resolved at the proxy. Use `HttpClient` with the configured proxy and a short timeout.

`doctor` checks: vault exists and mode 0600; config dir permissions; proxy configured/reachable; server URL policy; trust pins present; payload version; C core/Rust versions (Task 13 adds the F2F line); environment proxy variables present but ignored. Print `PASS`/`WARN`/`FAIL`; never print a secret. `--json` emits a stable object.

`completion bash|zsh|fish` prints a static script (top-level commands and globals). No network, no secrets.

`version --json` prints `{tool, core, print, f2f}`. `help [command]` prints the command help or the exit-code table for `help exit-codes`.

- [ ] **Step 4: Run tests to verify they pass**

Run:
```sh
cmake --build tools/hrt-tool/build --target doctor_test completion_test proxy_commands_test version_json_test
tools/hrt-tool/build/doctor_test && tools/hrt-tool/build/completion_test && tools/hrt-tool/build/proxy_commands_test && tools/hrt-tool/build/version_json_test
```
Expected: each prints `PASS`.

- [ ] **Step 5: Add the tests and run the suite**

Add `src/proxy_commands.cpp`, `src/doctor.cpp`, and `src/completion.cpp` to the `hrtcpp` source list and the four tests to CMake. Run `ctest --test-dir tools/hrt-tool/build --output-on-failure`. Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add tools/hrt-tool/src/proxy_commands.cpp tools/hrt-tool/src/doctor.cpp tools/hrt-tool/src/completion.h tools/hrt-tool/src/completion.cpp tools/hrt-tool/src/commands.h tools/hrt-tool/src/commands.cpp tools/hrt-tool/src/cli.cpp tools/hrt-tool/tests/doctor_test.cpp tools/hrt-tool/tests/completion_test.cpp tools/hrt-tool/tests/proxy_commands_test.cpp tools/hrt-tool/tests/version_json_test.cpp tools/hrt-tool/CMakeLists.txt
git commit -m "feat(hrt-tool): add proxy, doctor, completion, and help commands"
```

---

### Task 13: F2F as a Linked Go Library

**Files:**
- Create: `tools/hrt-f2f/lib.go`
- Create: `tools/hrt-tool/src/f2f_bridge.h`
- Create: `tools/hrt-tool/src/f2f_bridge.cpp`
- Modify: `tools/hrt-tool/src/commands.cpp`
- Modify: `tools/hrt-tool/CMakeLists.txt`
- Create: `tools/hrt-tool/tests/f2f_bridge_test.cpp`

**Interfaces:**
- Consumes: Go `run(args []string, stdout, stderr io.Writer) int` from `tools/hrt-f2f/main.go`.
- Produces in Go:
  ```c
  char *hrtf2f_version(void);
  int hrtf2f_run(int argc, char **argv);
  int hrtf2f_run_capture(int argc, char **argv,
                         char **out, size_t *out_len,
                         char **err_out, size_t *err_len);
  void hrtf2f_free(char *ptr);
  ```
  Produces in C++:
  ```cpp
  int run_f2f(const std::vector<std::string> &args, std::ostream &out,
              std::ostream &err);
  std::string f2f_version();
  ```

- [ ] **Step 1: Write the failing test**

```cpp
#include "f2f_bridge.h"
#include "test_framework.h"

#include <sstream>

int main() {
    std::ostringstream out, err;
    CHECK_EQ(run_f2f({"version"}, out, err), 0);
    CHECK(out.str().find("hrt-f2f") != std::string::npos);
    std::ostringstream help_out, help_err;
    CHECK_EQ(run_f2f({"--help"}, help_out, help_err), 0);
    CHECK(help_out.str().find("keygen") != std::string::npos);
    CHECK(!f2f_version().empty());
    return test_result();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build tools/hrt-tool/build --target f2f_bridge_test && tools/hrt-tool/build/f2f_bridge_test`
Expected: compile error, `run_f2f` not declared.

- [ ] **Step 3: Implement the Go exports and C++ bridge**

In `lib.go`, add `import "C"` with `//export` functions. Build `[]string` from `argv`, call `run` with `bytes.Buffer` writers, and return NUL-terminated copies via `C.CString`. `hrtf2f_free` calls `C.free`. Cap captured output at 128 MiB and return `F2F` with `F2F_LIMIT` if exceeded. Recover panics and return a non-zero code with an `F2F_INTERNAL` message in the error buffer.

In C++, `run_f2f` builds a `std::vector<char*>` of `const_cast<char*>(arg.c_str())`, calls `hrtf2f_run_capture`, prints both buffers to `out`/`err`, frees them with `hrtf2f_free`, and maps the Go code (`0`→`0`, `2`→`ExitCode::Usage`, other→`ExitCode::F2F`). `f2f_version` returns the Go version string. In CMake, use `file(GLOB HRTF2F_GO_SOURCES CONFIGURE_DEPENDS ${HRTF2F_DIR}/*.go)` for the dependency list; `DEPENDS` does not expand a glob itself.

In `commands.cpp`, dispatch `f2f` by calling `run_f2f(parsed.command_args, out, err)` **before any vault unlock**. Do not read the vault or active link. Add `f2f` to help, add an `f2f: <version>` line to the plain `version` output, extend `doctor` with the F2F version line, and extend `version_json_test` to assert the `f2f` key.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build tools/hrt-tool/build --target f2f_bridge_test && tools/hrt-tool/build/f2f_bridge_test`
Expected: `PASS`.

- [ ] **Step 5: Add the test, build the Go archive, and run the suite**

Add `src/f2f_bridge.cpp` to the `hrtcpp` source list, add the Go c-archive custom command, link the archive, and add `f2f_bridge_test`. Run `ctest --test-dir tools/hrt-tool/build --output-on-failure` and `GOPROXY=off go test -count=1 ./...` in `tools/hrt-f2f`. Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add tools/hrt-f2f/lib.go tools/hrt-tool/src/f2f_bridge.h tools/hrt-tool/src/f2f_bridge.cpp tools/hrt-tool/src/commands.cpp tools/hrt-tool/CMakeLists.txt tools/hrt-tool/tests/f2f_bridge_test.cpp
git commit -m "feat(hrt-tool): link the Go F2F library into hrt-tool"
```

---

### Task 14: CMake Install, `make update`, CI, and Build Options

**Files:**
- Modify: `tools/hrt-tool/CMakeLists.txt`
- Create: `tools/hrt-tool/Makefile`
- Modify: `tools/hrt-tool/configure`
- Modify: `.github/workflows/tools-hrt-tool.yml`
- Create: `tools/hrt-tool/scripts/install-smoke.sh`

**Interfaces:**
- Consumes: the `hrt-tool` target and its Go/Rust dependencies.
- Produces:
  - CMake options `HRT_TOOL_ENABLE_F2F` (ON), `HRT_TOOL_ENABLE_WARNINGS` (ON), `HRT_TOOL_ENABLE_WERROR` (OFF), `HRT_TOOL_ENABLE_SANITIZERS` (OFF).
  - `install(TARGETS hrt-tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})` plus README/docs.
  - Custom target `update` that builds `hrt-tool` and runs `cmake --install ${CMAKE_BINARY_DIR}`.
  - Source-tree Makefile targets `all`, `configure`, `build`, `test`, `install`, `update`, `clean` with `PREFIX ?= /usr/local`, `BUILD_DIR ?= build`.

- [ ] **Step 1: Write the failing smoke test**

`tools/hrt-tool/scripts/install-smoke.sh`:
```sh
#!/bin/sh
set -eu
build=${1:?build dir}
stage=${2:?stage dir}
cmake --install "$build" --prefix "$stage"
test -x "$stage/bin/hrt-tool"
"$stage/bin/hrt-tool" version
"$stage/bin/hrt-tool" f2f version
```

- [ ] **Step 2: Run it to verify it fails**

Run: `sh tools/hrt-tool/scripts/install-smoke.sh tools/hrt-tool/build /tmp/hrt-stage`
Expected: `install` does nothing / `stage/bin/hrt-tool` missing.

- [ ] **Step 3: Implement install/update and options**

Add `include(GNUInstallDirs)`, the install rules, and the `update` target. Add the four options and apply warnings/sanitizers to the C/C++ targets. Guard the Go build with `HRT_TOOL_ENABLE_F2F`; when OFF, compile a stub `f2f_bridge.cpp` that returns `ExitCode::Platform` with `F2F_DISABLED`. Add the Makefile wrapper. The Makefile `update` target must **not** depend on `configure`: it checks for `$(BUILD_DIR)/CMakeCache.txt`, prints the `make install` command if missing, and otherwise runs `cmake --build` and `cmake --install` directly so the cached prefix is used. Update `configure` to document `--prefix`.

- [ ] **Step 4: Run the smoke test**

Run:
```sh
cmake -S tools/hrt-tool -B tools/hrt-tool/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/tmp/hrt-prefix
cmake --build tools/hrt-tool/build --parallel
sh tools/hrt-tool/scripts/install-smoke.sh tools/hrt-tool/build /tmp/hrt-stage

make -C tools/hrt-tool install PREFIX=/tmp/hrt-make-stage BUILD_DIR=/tmp/hrt-make-build
test -x /tmp/hrt-make-stage/bin/hrt-tool
make -C tools/hrt-tool update BUILD_DIR=/tmp/hrt-make-build
test -x /tmp/hrt-make-stage/bin/hrt-tool
```
Expected: the CMake stage and the Makefile stage both contain `bin/hrt-tool`, and `make update` reinstalls to the cached `/tmp/hrt-make-stage` prefix without reconfigure.

- [ ] **Step 5: Update CI**

Add `actions/setup-go` with Go 1.23, keep `cargo fetch`, add the install smoke step, add a Linux `-DHRT_TOOL_ENABLE_WERROR=ON` build, and add an ASan/UBSan job with `-DHRT_TOOL_ENABLE_F2F=OFF -DHRT_TOOL_ENABLE_SANITIZERS=ON`.

- [ ] **Step 6: Commit**

```bash
git add tools/hrt-tool/CMakeLists.txt tools/hrt-tool/Makefile tools/hrt-tool/configure tools/hrt-tool/scripts/install-smoke.sh .github/workflows/tools-hrt-tool.yml
git commit -m "build(hrt-tool): install and update targets with CI coverage"
```

---

### Task 15: Documentation, Integration, and Release Script

**Files:**
- Modify: `README.md`
- Modify: `tools/hrt-tool/README.md`
- Modify: `tools/hrt-f2f/README.md`
- Modify: `CHANGELOG.md`
- Modify: `tools/hrt-f2f/scripts/build-release.sh`
- Create: `tools/hrt-tool/tests/console_integration_test.cpp`
- Modify: `tools/hrt-tool/CMakeLists.txt`

**Interfaces:**
- Consumes: the completed binary and console.
- Produces: user docs, a loopback console integration test, and a release artifact named `hrt-tool`.

- [ ] **Step 1: Write the failing integration test**

Create `console_integration_test.cpp`:
```cpp
#include "console.h"
#include "test_framework.h"
#include "vault.h"

#include <filesystem>
#include <sstream>

int main() {
    const auto dir = std::filesystem::temp_directory_path() / "hrt-console-integration";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    Vault::create(dir / "vault.hrtv", "correct horse battery staple");

    GlobalOptions options;
    options.config_dir = dir.string();
    std::istringstream in(
        "correct horse battery staple\n"
        "add-link hrt://127.0.0.1:9443 mtf\n"
        "commit-server mtf\n"
        "version\n"
        "exit\n"
        "exit\n");
    std::ostringstream out, err;
    CHECK_EQ(run_console(options, in, out, err), 0);
    CHECK(out.str().find("hrt$mtf>") != std::string::npos);
    CHECK(out.str().find("hrt-tool") != std::string::npos);
    std::filesystem::remove_all(dir);
    return test_result();
}
```
Register it in CMake next to `console_test`.

- [ ] **Step 2: Run it to verify it fails**

Expected: the prompt or link persistence is missing.

- [ ] **Step 3: Implement docs and release changes**

Document the console, links, status, interactive add, proxy normalisation, failure messages, exit codes, `make install`/`make update`, and the F2F namespacing. Update the release script to build/install `hrt-tool` and name the artifact `hrt-tool-<version>-<os>-<arch>`; keep the Go cross-build as the F2F library input where possible, and fail clearly on targets where the c-archive is unavailable.

- [ ] **Step 4: Run the full verification**

Run:
```sh
ctest --test-dir tools/hrt-tool/build --output-on-failure
GOPROXY=off go test -count=1 ./...   # in tools/hrt-f2f
sh tools/hrt-tool/scripts/install-smoke.sh tools/hrt-tool/build /tmp/hrt-stage
```
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add README.md tools/hrt-tool/README.md tools/hrt-f2f/README.md CHANGELOG.md tools/hrt-f2f/scripts/build-release.sh tools/hrt-tool/tests/console_integration_test.cpp tools/hrt-tool/CMakeLists.txt
git commit -m "docs(hrt-tool): document the merged console and update workflow"
```

---

## Self-Review

- **Spec coverage:** Every section of the design maps to a task: exit codes (1), proxy (2), global flags/f2f pass-through (3), vault links (4-5), single unlock (6), i18n (7), failures/retry/debug (8), console (9), status (10), interactive add (11), extra commands (12), F2F bridge (13), install/update/CI (14), docs/integration (15).
- **Step scan:** Each step names a file, a test assertion, or a command with an expected result. No TBD or "handle edge cases".
- **Type consistency:** `SessionContext`, `LinkData`, `UnlockedVault::session()`, `build_pk_view`, `build_status_view`, `run_f2f`, and `render_completion` are defined once and used with the same signatures in later tasks.
- **Review Focus:** Each of the five highlighted failure modes has a concrete test in Tasks 2/5/8/3+13/11.
- **Proportion:** The plan gives signatures, test assertions, and integration points; it does not transcribe implementation bodies.
