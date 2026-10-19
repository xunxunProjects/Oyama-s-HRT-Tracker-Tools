# hrt-tool M1 Skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a standalone, buildable `tools/hrt-tool` binary that unlocks an encrypted local vault, enforces the 40-bit master-password floor, writes encrypted logs, exposes the full command surface, and stubs the server API behind a typed interface.

**Architecture:** A standalone CMake project under `tools/hrt-tool/` builds a C11 core static library (`hrtcore`) for OpenSSL crypto primitives, a C++17 executable for CLI/config/vault/HTTP, and a dependency-free Rust static library (`hrtprint`) for output. Nothing is added to the root `hrtcold` CMake project. The server API is an abstract `ApiClient` with a `StubApiClient` in M1.

**Tech Stack:** C11, C++17, CMake ≥ 3.16, OpenSSL (libcrypto), libcurl, nlohmann/json, Rust (staticlib), CTest.

**Spec:** `docs/superpowers/specs/2026-10-19-hrt-tool-design.md`

## Global Constraints

- C11, C++17, CMake ≥ 3.16; OpenSSL, libcurl, nlohmann/json, Rust toolchain.
- hrt-tool is a standalone project under `tools/hrt-tool/`. Do not modify the root `CMakeLists.txt`, `configure`, `cmake/`, `include/hrtcold/`, `tests/native/`, or `tools/hrt-cold/`.
- Vault: `HRTV` header, PBKDF2-SHA256 600,000 iterations, 16-byte salt, AES-256-GCM with the 48-byte header as AAD, 1 MiB max, mode 0600, atomic replace.
- Master password: both the website guess model and the uniform `L x log2(N)` model must be ≥ 40 bits.
- All persistent state is encrypted. Plaintext is written only to stdout.
- No `system`, `popen`, `exec`, `dlopen`, shell invocation, or evaluation of server data.
- Limits: 16 MiB parse, 100,000 events, 10,000 labs, 1,000 query points, 64 KiB per log line.
- Every placeholder commit must build and run; fix failures before the next task.
- Commit messages end with: `Co-Authored-By: Claude Code <noreply@anthropic.com>`

## Review Focus

1. A wrong or tampered vault password must fail closed before any state is read.
2. The entropy gate must reject common passwords, repeats, keyboard walks, and short strings.
3. No plaintext secret may reach a regular file: vault, logs, exports, or temp files.
4. Malformed or oversized input must fail with a typed error, never crash or OOM.
5. A crash or write failure during vault save must leave the previous vault intact.

---

### Task 1: Standalone build and first runnable binary

**Files:**
- Create: `tools/hrt-tool/CMakeLists.txt`
- Create: `tools/hrt-tool/configure`
- Create: `tools/hrt-tool/include/hrtcore/hrtcore.h`
- Create: `tools/hrt-tool/native/core/version.c`
- Create: `tools/hrt-tool/include/hrtprint/hrtprint.h`
- Create: `tools/hrt-tool/rust/hrt-print/Cargo.toml`
- Create: `tools/hrt-tool/rust/hrt-print/src/lib.rs`
- Create: `tools/hrt-tool/src/main.cpp`
- Create: `tools/hrt-tool/src/version.h`
- Create: `tools/hrt-tool/src/version.cpp`
- Test: `tools/hrt-tool/tests/version_test.cpp`

**Interfaces:**
- Produces: `const char *hrtcore_version(void)`, `uint32_t hrtcore_abi_version(void)`, `const char *hrtprint_version(void)`, `ToolVersion current_version()`.

- [ ] **Step 1: Write the failing version test**

`tests/version_test.cpp` calls `hrtcore_version()`, `hrtcore_abi_version()`, `hrtprint_version()`, and `current_version()`; assert the first three are `"0.1.0"` (ABI `1`) and `current_version()` returns `{0,1,0}`.

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake -S tools/hrt-tool -B tools/hrt-tool/build && cmake --build tools/hrt-tool/build`
Expected: configure/build fails because the headers and targets do not exist.

- [ ] **Step 3: Implement the ABI headers, C and Rust libraries, and CMake**

`hrtcore/hrtcore.h` declares the two version functions with `extern "C"`. `native/core/version.c` returns `"0.1.0"` and `1`. `hrtprint.h` declares `const char *hrtprint_version(void)`. The Rust crate is `crate-type = ["staticlib"]`, no dependencies, exporting `#[no_mangle] pub extern "C" fn hrtprint_version() -> *const c_char` backed by `b"0.1.0\0"`.

`CMakeLists.txt` finds OpenSSL, CURL, nlohmann_json, and cargo; builds `hrtcore` as a static library; builds the Rust crate with `cargo build --release --offline`; imports the static library; builds `hrt-tool`; registers tests with CTest.

- [ ] **Step 4: Implement `main.cpp` and `version`**

`main.cpp` calls `run_cli(argc, argv, std::cin, std::cout, std::cerr)`. For this task, `run_cli` only supports `version`: print `hrt-tool 0.1.0`, `hrtcore 0.1.0 (ABI 1)`, `hrtprint 0.1.0`.

- [ ] **Step 5: Build, test, and run**

Run: `cmake -S tools/hrt-tool -B tools/hrt-tool/build && cmake --build tools/hrt-tool/build && (cd tools/hrt-tool/build && ctest --output-on-failure) && tools/hrt-tool/build/hrt-tool version`
Expected: CTest passes; the binary prints all three version lines and exits 0.

- [ ] **Step 6: Commit**

```bash
git add tools/hrt-tool
git commit -m "feat(tools): add standalone hrt-tool build skeleton"
```

---

### Task 2: Crypto primitives in the C core

**Files:**
- Modify: `tools/hrt-tool/include/hrtcore/hrtcore.h`
- Create: `tools/hrt-tool/native/core/crypto.c`
- Create: `tools/hrt-tool/native/core/base64.c`
- Create: `tools/hrt-tool/tests/test_framework.h`
- Create: `tools/hrt-tool/tests/crypto_test.cpp`

**Interfaces:**
- Produces: `hrtcore_error`, `hrtcore_secure_zero`, `hrtcore_random_bytes`, `hrtcore_b64_encode/decode`, `hrtcore_pbkdf2_sha256`, `hrtcore_hkdf_sha256`, `hrtcore_aes256gcm_encrypt/decrypt`, `hrtcore_etm_encrypt/decrypt`.
- Error codes: `HRTCORE_OK=0`, `INVALID_ARG=1`, `BUFFER_TOO_SMALL=2`, `CRYPTO=3`, `AUTH=4`, `LIMIT=5`.

- [ ] **Step 1: Write the failing crypto tests**

Use `tests/test_framework.h` (a `CHECK` macro and a `main` that counts failures). Assert:
- RFC 4648 base64 `"f"` → `"Zg=="`, `"foobar"` → `"Zm9vYmFy"`.
- PBKDF2-HMAC-SHA256 `"password"`/`"salt"`/1/32 → hex `120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b`; 2 iterations → `ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43`.
- RFC 5869 HKDF-SHA256 test case 1: OKM begins `3cb25f25faacd57a90434f64d0362f2a` and is 42 bytes.
- AES-256-GCM zero key/nonce with empty plaintext/AAD produces the 16-byte tag `530f8afbc74536b9a963b4f1c4cb738b`.
- A GCM round-trip of `"hello"` succeeds; a flipped ciphertext byte returns `HRTCORE_ERR_AUTH`.
- An EtM round-trip succeeds; tampering with ciphertext, MAC, or AAD returns `HRTCORE_ERR_AUTH`.
- A 1-byte output buffer returns `HRTCORE_ERR_BUFFER_TOO_SMALL`.
- `hrtcore_secure_zero` zeroes a 32-byte buffer.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build tools/hrt-tool/build && (cd tools/hrt-tool/build && ctest -R crypto --output-on-failure)`
Expected: link failure for the missing symbols.

- [ ] **Step 3: Implement the primitives**

Use `EVP_DigestSign`/`HMAC` for PBKDF2 (`PKCS5_PBKDF2_HMAC`), `EVP_PBE_scrypt` is not needed. Use `EVP_EncryptInit_ex`/`EVP_DecryptInit_ex` with `EVP_aes_256_gcm` and `EVP_aes_256_cbc`. Base64 via `EVP_EncodeBlock`/`EVP_DecodeBlock` with explicit padding handling. HMAC via `HMAC(EVP_sha256(), ...)`. Constant-time compare via `CRYPTO_memcmp`. HKDF via `EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL)`. All functions check capacities before writing and set `err->message`.

- [ ] **Step 4: Run the tests**

Run: `cmake --build tools/hrt-tool/build && (cd tools/hrt-tool/build && ctest -R crypto --output-on-failure)`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/hrt-tool/include tools/hrt-tool/native tools/hrt-tool/tests
git commit -m "feat(hrt-tool): add OpenSSL crypto primitives and tests"
```

---

### Task 3: Master-password entropy gate

**Files:**
- Create: `tools/hrt-tool/src/entropy.h`
- Create: `tools/hrt-tool/src/entropy.cpp`
- Create: `tools/hrt-tool/tests/entropy_test.cpp`

**Interfaces:**
- Produces: `struct EntropyEstimate { double guess_bits; double uniform_bits; int code_points; int charset_size; };`, `EntropyEstimate estimate_entropy(const std::string&)`, `bool meets_master_password_floor(const EntropyEstimate&)`, `double empirical_shannon_bits(const std::string&)`.

- [ ] **Step 1: Write the failing tests**

Assert:
- `"password"`, `"aaaaaaaaaaaaaaaa"`, `"qwertyuiop"`, `"12345678"`, `"Password123!"` have `guess_bits < 40`.
- `"correct horse battery staple"` has both estimates ≥ 40.
- A fixed random-looking 12-character lower+upper+digit string has both ≥ 40.
- `"abcdefghijkl"` has `uniform_bits` within `1e-9` of `12 * log2(26)`.
- A UTF-8 CJK password reports `code_points` equal to its Unicode code point count, not byte count.
- `empirical_shannon_bits("ab")` is `2.0` and `empirical_shannon_bits("aaaa")` is `0.0`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build tools/hrt-tool/build && (cd tools/hrt-tool/build && ctest -R entropy --output-on-failure)`
Expected: link failure.

- [ ] **Step 3: Port the website estimator**

Port `estimatePasswordBits`, `effectiveLength`, `patternAt`, `poolSize`, the `COMMON` set, `ROWS`, `LEET`, and `DATES` from `/home/haiyan/git/Oyama-s-HRT-Tracker/passwordStrength.ts` verbatim in behavior. Decode UTF-8 to code points before counting/classifying; `charsetSize` uses 26/26/10/33/100 and a minimum pool of 10. Implement `uniform_bits` as `code_points * log2(charset_size)`. Implement `empirical_shannon_bits` as `-sum(count_i * log2(count_i / n))` over code points. `meets_master_password_floor` requires both estimates ≥ 40.0.

- [ ] **Step 4: Run the tests**

Run: `cmake --build tools/hrt-tool/build && (cd tools/hrt-tool/build && ctest -R entropy --output-on-failure)`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/hrt-tool/src/entropy.* tools/hrt-tool/tests/entropy_test.cpp
git commit -m "feat(hrt-tool): add master password entropy gate"
```

---

### Task 4: Encrypted vault

**Files:**
- Create: `tools/hrt-tool/src/paths.h`
- Create: `tools/hrt-tool/src/paths.cpp`
- Create: `tools/hrt-tool/src/vault.h`
- Create: `tools/hrt-tool/src/vault.cpp`
- Create: `tools/hrt-tool/tests/vault_test.cpp`

**Interfaces:**
- Produces: `struct VaultData { int version; std::string token, user_id, username, device_pass, cloud_key, server, proxy; nlohmann::json trust; bool opsec_ack; int64_t created_at, updated_at; };`
- `class VaultSession { VaultData data; std::array<uint8_t,32> key; std::array<uint8_t,32> log_key; ~VaultSession(); };`
- `class Vault { static bool exists(path); static VaultSession create(path, password); static VaultSession unlock(path, password); static void save(path, const VaultData&, const std::string& password); };`
- `struct Paths { std::filesystem::path config_dir, vault_file; }; Paths resolve_paths(const std::string& override);`

- [ ] **Step 1: Write the failing vault tests**

Use a unique temporary directory per test. Assert:
- `create` then `unlock` returns the same `VaultData` and a 32-byte key/log key.
- The vault file starts with `HRTV`, is ≤ 1 MiB, and has mode 0600 on Unix.
- The file bytes never contain the JSON field names or the token value.
- A wrong password throws `VaultAuthError`.
- Flipping one ciphertext byte throws `VaultAuthError`.
- Changing the iteration count or salt in the header throws `VaultAuthError` (AAD binding).
- A password with `guess_bits < 40` or `uniform_bits < 40` throws `VaultWeakPasswordError` and creates no file.
- `save` updates the vault and leaves no `*.tmp` file.
- A save into a read-only directory fails and leaves the previous vault bytes unchanged.
- `resolve_paths` honors `XDG_CONFIG_HOME` and an explicit override.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build tools/hrt-tool/build && (cd tools/hrt-tool/build && ctest -R vault --output-on-failure)`
Expected: link failure.

- [ ] **Step 3: Implement paths and the vault format**

Header (48 bytes, big-endian): magic `HRTV`, u16 version 1, u16 kdf 1, u32 iterations 600000, u16 salt_len 16, salt, u16 nonce_len 12, nonce, u32 ciphertext_len. AAD is the first 44 bytes. Plaintext is nlohmann JSON. Derive the log key with `hrtcore_hkdf_sha256(key, "", "hrt-tool-log-v1", 32)`. Write with `open(O_CREAT|O_EXCL, 0600)` to a temp path, `fsync`, `rename`, then `fsync` the directory. Zeroize the derived key in `VaultSession::~VaultSession`.

- [ ] **Step 4: Run the tests**

Run: `cmake --build tools/hrt-tool/build && (cd tools/hrt-tool/build && ctest -R vault --output-on-failure)`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/hrt-tool/src/paths.* tools/hrt-tool/src/vault.* tools/hrt-tool/tests/vault_test.cpp
git commit -m "feat(hrt-tool): add encrypted local vault"
```

---

### Task 5: Encrypted logs

**Files:**
- Create: `tools/hrt-tool/src/log.h`
- Create: `tools/hrt-tool/src/log.cpp`
- Create: `tools/hrt-tool/tests/log_test.cpp`

**Interfaces:**
- Produces: `class EncryptedLog { static void append(path, const std::array<uint8_t,32>& log_key, const std::string& line); static void show(path, const std::array<uint8_t,32>& log_key, std::ostream&); };`

- [ ] **Step 1: Write the failing tests**

Assert:
- Two appended lines are returned in order by `show`.
- The file bytes do not contain either plaintext line.
- A wrong log key throws `LogAuthError`.
- Flipping a ciphertext byte throws.
- A truncated final record throws.
- A line longer than 64 KiB is rejected with `ToolLimitError`.
- An empty file shows nothing.
- A record whose declared length is larger than the file throws instead of allocating.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build tools/hrt-tool/build && (cd tools/hrt-tool/build && ctest -R log --output-on-failure)`
Expected: link failure.

- [ ] **Step 3: Implement the record format**

Each record: `u32 payload_len BE` (8 + 12 + ciphertext_len), `u64 timestamp_ms BE`, 12-byte nonce, AES-256-GCM ciphertext+tag. AAD is the first 24 bytes (length + timestamp + nonce). Append with `O_APPEND|O_CREAT`, mode 0600. `show` reads sequentially, rejects lengths over 64 KiB + overhead, and writes decrypted lines to the stream.

- [ ] **Step 4: Run the tests**

Run: `cmake --build tools/hrt-tool/build && (cd tools/hrt-tool/build && ctest -R log --output-on-failure)`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/hrt-tool/src/log.* tools/hrt-tool/tests/log_test.cpp
git commit -m "feat(hrt-tool): add encrypted debug logs"
```

---

### Task 6: CLI surface, password prompts, and runnable stubs

**Files:**
- Create: `tools/hrt-tool/src/cli.h`
- Create: `tools/hrt-tool/src/cli.cpp`
- Create: `tools/hrt-tool/src/error.h`
- Create: `tools/hrt-tool/src/prompt.h`
- Create: `tools/hrt-tool/src/prompt.cpp`
- Create: `tools/hrt-tool/src/commands.h`
- Create: `tools/hrt-tool/src/commands.cpp`
- Modify: `tools/hrt-tool/src/main.cpp`
- Test: `tools/hrt-tool/tests/cli_test.cpp`

**Interfaces:**
- Produces: `int run_cli(int argc, const char* const* argv, std::istream& in, std::ostream& out, std::ostream& err);`
- `std::string read_password(std::istream& in, std::ostream& out, const std::string& prompt, bool confirm, bool no_echo);`
- Exit codes: `0` ok, `2` usage, `3` vault/auth, `4` network, `5` data, `6` not implemented.

- [ ] **Step 1: Write the failing CLI tests**

Assert:
- `version` returns 0 and prints the three version lines.
- `--help` returns 0 and lists `init`, `login`, `look`, `add`, `export`, `import`, `snapshot`, `config`, `logs`, `version`.
- An unknown command returns 2.
- `config path --config-dir <tmp>` prints the config dir and vault path without unlocking.
- `init --config-dir <tmp> --vault-password-stdin` with a strong password creates the vault and returns 0.
- The same `init` with `"aaaaaaaaaaaaaaaa"` returns 3 and creates no vault.
- `login` without a vault returns 3 and says `run hrt-tool init`.
- `login` with a vault returns 6 and says `not implemented in M1`.
- Every stubbed command (`logout`, `whoami`, `look`, `add`, `dose`, `lab`, `weight`, `export`, `import`, `snapshot`, `logs show`) returns 6 or a clear data error, never a crash.
- `--vault-password-file` pointing at a mode-0644 file returns 3 on Unix.
- `login --debug --log-file <tmp>` unlocks the vault, writes an encrypted log record for the stub failure, and the file bytes contain no plaintext; `logs show --log-file <tmp>` decrypts it.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build tools/hrt-tool/build && (cd tools/hrt-tool/build && ctest -R cli --output-on-failure)`
Expected: link failure.

- [ ] **Step 3: Implement the parser and dispatcher**

Parse global flags (`--server`, `--proxy`, `--config-dir`, `--debug`, `--trace`, `--json`, `--no-color`, `--yes`, `--timeout`, `--trust-server`, `--insecure-tls`, `--transport`, `--no-cache-cloud-key`, `--compute`, `--vault-password-file`, `--vault-password-stdin`, `--log-file`) before the command; command flags after. Implement `init`, `config path`, `logs show`, and `version`; route the rest to stubs that unlock the vault and report `not implemented in M1`. `read_password` uses termios on Unix for no-echo prompts and falls back to `std::getline` when `in` is not a TTY; tests pass `std::istringstream`.

- [ ] **Step 4: Run the tests and the binary**

Run: `cmake --build tools/hrt-tool/build && (cd tools/hrt-tool/build && ctest --output-on-failure)`
Then run `tools/hrt-tool/build/hrt-tool version`, `... --help`, `... --config-dir /tmp/hrt-tool-test init --vault-password-stdin`, and `... --config-dir /tmp/hrt-tool-test login`.
Expected: tests pass; the manual commands produce the documented output and exit codes.

- [ ] **Step 5: Commit**

```bash
git add tools/hrt-tool/src tools/hrt-tool/tests/cli_test.cpp
git commit -m "feat(hrt-tool): add CLI surface and runnable M1 stubs"
```

---

### Task 7: HTTP transport, URL policy, and trust store

**Files:**
- Create: `tools/hrt-tool/src/http.h`
- Create: `tools/hrt-tool/src/http.cpp`
- Create: `tools/hrt-tool/src/trust.h`
- Create: `tools/hrt-tool/src/trust.cpp`
- Create: `tools/hrt-tool/tests/http_test.cpp`
- Create: `tools/hrt-tool/tests/trust_test.cpp`

**Interfaces:**
- Produces: `struct Url { std::string scheme, host, port, path; }; Url parse_url(const std::string&); void validate_server_url(const Url&); void validate_proxy_url(const std::string&);`
- `struct HttpResponse { long status; std::string body; std::multimap<std::string,std::string> headers; };`
- `struct HttpOptions { std::string proxy; bool debug = false; bool trace = false; long timeout_s = 30; bool insecure_tls = false; };`
- `class HttpClient { HttpResponse request(const std::string& method, const std::string& url, const std::vector<std::string>& headers, const std::string& body, const HttpOptions&); };`
- `enum class TrustState { Unknown, Trusted, Changed }; class TrustStore { explicit TrustStore(nlohmann::json& store); TrustState check(const std::string& origin, const std::string& spki) const; void trust(const std::string& origin, const std::string& spki); };`
- `std::string redact_header_line(const std::string& header_line);`

- [ ] **Step 1: Write the failing tests**

Assert:
- `https://hrt.mahiro.uk` parses with port 443; `https://example.com:8443/api` keeps 8443 and `/api`.
- Non-http(s) schemes are rejected.
- `http://127.0.0.1:8787` is allowed; `http://example.com` is rejected; `http://abc.onion` and `http://abc.i2p` are allowed.
- `socks5h://127.0.0.1:9050`, `socks5://127.0.0.1:4447`, `http://127.0.0.1:8080` are accepted proxies; `ftp://` is rejected.
- Trust: unknown → trusted → changed; a changed SPKI returns `Changed` until re-trusted.
- `redact_authorization("Authorization: Bearer abc")` contains no `abc`.
- `HttpClient` rejects a request to `http://example.com` before any socket is opened.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build tools/hrt-tool/build && (cd tools/hrt-tool/build && ctest -R 'http|trust' --output-on-failure)`
Expected: link failure.

- [ ] **Step 3: Implement URL policy, trust, and the libcurl wrapper**

Use libcurl easy handles. Set `CURLOPT_PROXY`, `CURLOPT_PROXYTYPE` for SOCKS5/SOCKS5H, `CURLOPT_SSL_VERIFYPEER/HOST`, `CURLOPT_TIMEOUT`, `CURLOPT_FOLLOWLOCATION` with `CURLOPT_MAXREDIRS=3`, and `CURLOPT_VERBOSE` only for `--trace`. Use `CURLOPT_SSL_CTX_FUNCTION` with `SSL_get_peer_certificate` and `X509_digest(EVP_sha256())` to obtain the SPKI digest (use `X509_PUBKEY` + `i2d_PUBKEY`). Redact `Authorization`, `Cookie`, `Set-Cookie`, and JSON keys `token`, `password`, `device_pass`, `cloud_key` before printing. Never print the body unless `--debug-body` is explicitly added later; in M1, never print it.

- [ ] **Step 4: Run the tests**

Run: `cmake --build tools/hrt-tool/build && (cd tools/hrt-tool/build && ctest -R 'http|trust' --output-on-failure)`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/hrt-tool/src/http.* tools/hrt-tool/src/trust.* tools/hrt-tool/tests/http_test.cpp tools/hrt-tool/tests/trust_test.cpp
git commit -m "feat(hrt-tool): add URL policy, trust store, and HTTP transport"
```

---

### Task 8: Typed API interface with M1 stub

**Files:**
- Create: `tools/hrt-tool/src/api.h`
- Create: `tools/hrt-tool/src/api_stub.cpp`
- Create: `tools/hrt-tool/tests/api_test.cpp`

**Interfaces:**
- Produces: `struct LoginResult { std::string token, user_id, username, device_pass; int api_version; };`
- `struct Snapshot { std::string id; int64_t created_at; nlohmann::json data; };`
- `class ApiClient` with pure virtual `login`, `logout`, `me`, `latest_snapshot`, `put_snapshot`, `server_version`.
- `class StubApiClient : public ApiClient`; `std::unique_ptr<ApiClient> make_api_client(const std::string& server, const HttpOptions&);`

- [ ] **Step 1: Write the failing tests**

Assert:
- `StubApiClient::server_version()` returns `{"apiVersion":1,"transport":["tls","openssl-v1"]}`.
- Every other stub method throws `ToolError` with exit code 6 and a message containing `not implemented in M1`.
- `make_api_client` returns a non-null stub in M1.
- The stub opens no socket (test with an invalid `--server` that would fail URL validation if used; the stub ignores it).

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build tools/hrt-tool/build && (cd tools/hrt-tool/build && ctest -R api --output-on-failure)`
Expected: link failure.

- [ ] **Step 3: Implement the interface and stub**

Define the abstract interface and the stub. Do not include `http.h` in the stub. `ToolError` lives in `src/error.h` (created in Task 6) with `int exit_code`, `std::string code`, `std::string message`.

- [ ] **Step 4: Run the tests**

Run: `cmake --build tools/hrt-tool/build && (cd tools/hrt-tool/build && ctest -R api --output-on-failure)`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/hrt-tool/src/api.h tools/hrt-tool/src/api_stub.cpp tools/hrt-tool/tests/api_test.cpp
git commit -m "feat(hrt-tool): add typed API interface and M1 stub"
```

---

### Task 9: README, CI, and full M1 verification

**Files:**
- Create: `tools/hrt-tool/README.md`
- Create: `.github/workflows/tools-hrt-tool.yml`
- Create: `tools/hrt-tool/advanced/gpg_f2f/README.md`
- Create: `tools/hrt-tool/advanced/gpg_f2f/gpg_f2f.py`

- [ ] **Step 1: Write the README and placeholder**

README covers: status `M1 skeleton`; build commands; `init`/unlock; the 40-bit gate; the vault and log formats; the full command table with M1 stubs marked; security notes (honest-server password disclosure, no shell, no plaintext at rest); and the M2-M4 roadmap. The GPG placeholder is a Python file with a module docstring explaining face-to-face key exchange, `raise NotImplementedError`, and comments that it is not built or invoked.

- [ ] **Step 2: Write the CI workflow**

`.github/workflows/tools-hrt-tool.yml` triggers on `tools/hrt-tool/**`. Ubuntu job: checkout, setup Rust, `apt-get install -y cmake pkg-config libssl-dev libcurl4-openssl-dev nlohmann-json3-dev`, configure, build, `ctest --output-on-failure`. Add a Windows job as a follow-up placeholder with `continue-on-error: true` and a comment that it is enabled in M4.

- [ ] **Step 3: Run the full verification**

Run:
```bash
cmake -S tools/hrt-tool -B tools/hrt-tool/build
cmake --build tools/hrt-tool/build
(cd tools/hrt-tool/build && ctest --output-on-failure)
tools/hrt-tool/build/hrt-tool version
./tools/hrt-tool/build/hrt-tool --config-dir /tmp/hrt-tool-m1 init --vault-password-stdin <<<'correct horse battery staple'
./tools/hrt-tool/build/hrt-tool --config-dir /tmp/hrt-tool-m1 config path
./tools/hrt-tool/build/hrt-tool --config-dir /tmp/hrt-tool-m1 login --vault-password-stdin <<<'correct horse battery staple'
```
Expected: all tests pass; `version` prints three lines; `init` succeeds; `config path` prints paths; `login` exits 6 with `not implemented in M1`.

- [ ] **Step 4: Verify hrt-cold is untouched**

Run: `git status --short && git diff --stat`
Expected: only `tools/hrt-tool/**` and the docs plan/spec are changed; `tools/hrt-cold/`, root CMake, and `include/hrtcold/` are unchanged.

- [ ] **Step 5: Commit**

```bash
git add tools/hrt-tool .github/workflows/tools-hrt-tool.yml
git commit -m "docs(hrt-tool): document M1 and add CI"
```

---

## M1 Completion Criteria

- `ctest` passes all C, C++, and Rust integration tests.
- `hrt-tool version`, `init`, `config path`, and `logs show` work.
- Every other command exits 6 with a clear "not implemented in M1" message.
- Vault and log files contain no plaintext secrets.
- A wrong or weak password fails closed.
- `hrt-cold` and the root native scaffold are unchanged.
- CI workflow is committed and runs on Ubuntu.
