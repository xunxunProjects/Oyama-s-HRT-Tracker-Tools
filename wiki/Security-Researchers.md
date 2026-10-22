# Security researchers

This page is the entry point for testing `hrt-tool`, `hrt-cold`, and
`hrt-f2f`. The web app and Worker have their own
[security research page](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker/wiki/Security-Researchers).

## Which version to test

- **Latest commit** on `main` for new fixes and unreleased behaviour. As of
  2026-10-22 the tip is `015264e` (terminal capability layer and console
  banner).
- **Published release assets** for reproducible testing. The current Tools
  release line is **0.1.0** (`hrt-cold`, `hrt-f2f`); `hrt-tool` 0.1.1 is a
  pre-release and its packages may be rebuilt.
- Always record the exact commit or asset hash in your report.

## Scope and known results

The static audit so far (2026-10-22) found:

| Area | Result |
| --- | --- |
| Subprocess / SSH / Git invocation | No production `system`, `popen`, `exec*`, `CreateProcess`, `os/exec`, `ssh`, `git`, `unzip`, or `tar -x`. Only `tools/hrt-f2f/interop_test.go` runs `gpg` in a test. |
| Token-to-host binding | Tokens are stored per link; changing the origin clears the token and device identity; a different explicit `--server` is refused (`SERVER_MISMATCH`). |
| File paths / extraction | Output paths are user-supplied and guarded; import requires a regular file, caps at 16 MiB, and performs no archive extraction. hrt-f2f writes mode 0600 with `O_EXCL` or temp+rename. |
| URL parsing | `parse_url` requires a scheme, rejects userinfo, supports bracketed IPv6, and accepts numeric ports 1–65535. hrt-f2f accepts https, or http only for loopback, and socks5h proxies. |
| Terminal output | Rendered drug/route/lab values come from enums or allowlists; template names are never printed. Control characters from imports are not reachable in rendered tables. |
| Config / environment | No `HTR_*` environment configuration; only `APPDATA`/`XDG_CONFIG_HOME`/`HOME` and proxy variables. |
| JSON / dependencies | Import is sanitised and capped; malformed JSON errors are handled. No Tools Dependabot alerts are open. |

One hardening observation (not a reproduced finding): the Go HTTP clients do
not set `CheckRedirect`, so Go's default redirect policy applies. An open
redirect on a sibling subdomain could carry the bearer token; no such
redirect was found. Treat it as a review item if you add redirects.

The web/Worker audit found two Low-severity issues (an `alg:none` JWT causing
a 500, and missing `Cache-Control: no-store` on three private GETs); both are
fixed in the main repo. Those are not CLI findings; see the main wiki for the
evidence.

## Test safely

1. Build from the commit or use the release asset; record the version.
2. Use synthetic vaults, snapshots, and accounts. Never point tests at a
   production server or real data.
3. For the API, start a local Worker on `127.0.0.1` and use a throwaway
   `--config-dir`. Do not use port 8787 on a machine where another service
   owns it.
4. Prefer `--snapshot` workflows for data-format tests; they need no server.
5. Do not run destructive imports, `init --reset`, or `--replace` against
   anything you did not create. Keep backups.
6. Report with the exact command, input, output, exit code, and version.

## Reporting

Use the private advisory form:
<https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/security/advisories/new>

Acknowledged within 7 days; English or Chinese. See
[Reporting Vulnerabilities](Reporting-Vulnerabilities) and
[`SECURITY.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/blob/main/SECURITY.md).

Related: [Security Model](Security-Model), [Privacy Environments](Privacy-Environments),
[Development Guide](Development-Guide).
