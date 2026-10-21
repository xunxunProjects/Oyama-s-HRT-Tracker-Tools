# Development Guide

## Repository layout

```text
tools/hrt-tool/          C11/C++17 terminal client
  src/                   CLI, commands, console, HTTP/raw client, vault
  native/core/           C core: PK engine, base64, version, crypto helpers
  rust/hrt-print/        Rust static library for status/table printing
  tests/                 C++ regression tests
  cmake/, CMakeLists.txt build and install rules
tools/hrt-f2f/           Go OpenPGP/F2F implementation (c-archive)
tools/hrt-cold/          Go offline cold-backup CLI
docs/native/             Native core design, ABI, crypto, build, test plan
docs/hrt-protocol/       Raw hrt/1 protocol and F2F service
docs/tool-interface/     /api/tool/v1 contract
docs/backup-schema-audit/Website ↔ Tools hand-off
wiki/                    Source for the GitHub wiki
```

## Language boundaries

- **C core** (`native/core`): portable PK math and primitives with a stable C
  ABI (`HRTCORE_ABI_VERSION`). No CLI or network code.
- **C++ CLI** (`src`): commands, console, vault, HTTP/raw transport, trust,
  links. C++17, no exceptions across the C ABI.
- **Rust printer** (`rust/hrt-print`): render tables and status output behind
  a small C ABI.
- **Go F2F** (`tools/hrt-f2f`): OpenPGP, keyring, session. Built as a
  `c-archive` and linked into `hrt-tool`; called through `src/f2f_bridge.cpp`.
- **Go cold** (`tools/hrt-cold`): standalone offline backup CLI.

## Build and test

```sh
cmake -S tools/hrt-tool -B tools/hrt-tool/build -DCMAKE_BUILD_TYPE=Debug
cmake --build tools/hrt-tool/build --parallel
ctest --test-dir tools/hrt-tool/build --output-on-failure

cd tools/hrt-f2f && go test -count=1 ./...
cd tools/hrt-cold && go test -count=1 ./...
cargo test --offline --manifest-path tools/hrt-tool/rust/hrt-print/Cargo.toml
```

CI runs a Linux build and tests, Go tests, an install smoke test, a
warnings-as-errors build, and ASan/UBSan with F2F disabled.

## Invariants

- No `system`, `popen`, `exec`, or `dlopen`.
- No plaintext secrets in logs, files, or process arguments.
- Vault and secret files are `0600` on Unix, atomic, and encrypted at rest.
- `socks5h` semantics: the proxy resolves every name; environment proxies are
  ignored.
- Tokens are origin-bound; server keys are pinned on first contact.
- F2F private keys never leave the machine.
- No device fingerprinting or global ban list; the raw server is opt-in.
- No custom cryptography; use the selected OpenPGP/OpenSSL primitives.

## Tests

- Add a regression test for every behavior change; run the full CTest suite.
- Use synthetic data only. Never commit real health records, keys,
  passphrases, tokens, or backups.
- Failure paths matter: permission, network, trust, replay, and cancellation
  tests are part of the suite.

## Pull requests

- Small, focused changes with tests are easiest to review.
- Include what changed, why, exact commands run, results, untested platforms,
  and confirmation that no secrets are included.
- Keep documentation in English. User-facing release notes may include a short
  Chinese summary.
- Do not force-push or rewrite shared branches.

See [`CONTRIBUTING.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/blob/main/CONTRIBUTING.md).

## Canonical sources

- `CONTRIBUTING.md`
- `docs/native/TEST_PLAN.md`
- `docs/native/DESIGN.md`
- `.github/workflows/`
