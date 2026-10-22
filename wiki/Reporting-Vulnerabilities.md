# Reporting Vulnerabilities

How to report a security problem in `hrt-tool`, `hrt-cold`, `hrt-f2f`, or the
release files from this repository, and how to test without putting anyone's
data at risk. The policy itself is
[`SECURITY.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/blob/main/SECURITY.md);
this page is the practical guide.

**Do not report a security problem in a public issue, discussion, or pull
request.** Never send real health data, private keys, passphrases, tokens,
vaults, or production backups, not even in a private report.

## Where to report

| Problem in | Report to |
| --- | --- |
| `hrt-tool`, `hrt-cold`, `hrt-f2f` (`hrt-tool f2f`), the native scaffold, build and release scripts, published archives and checksum files, CI workflows, or this wiki | This repository: **Security → Report a vulnerability** |
| The web app, the Worker API (including `/api/tool/v1/` and the F2F service), the `hrt-server` raw `hrt/1` adapter, the Docker image, or the desktop app | The tracker repository: [report a vulnerability](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker/security/advisories/new) |
| A linked library (OpenPGP, libcurl, OpenSSL, the Go standard library) | That library's maintainers first, then tell us if the impact reaches these tools |
| A server someone else runs | Its operator; tell us too if the cause is in our code |

If a problem involves both the client and the server, for example the two
sides disagree about a protocol rule, report it once in either repository and
say that it affects both.

If **Report a vulnerability** is not shown on this repository, open a minimal
public issue that asks the maintainers for a private channel. Put no details
of the problem in that issue.

## What is in scope

- `hrt-tool`: the encrypted vault and its file permissions, server trust and
  SPKI pinning, the trust probe and redirect handling, proxy and DNS handling
  (including Tor and I2P), the `hrt/1` client and its device key, login and
  session handling, local snapshots, HRTE export and import, encrypted logs,
  and the console.
- `hrt-cold`: `.hrtb` containers, complete-backup envelope validation,
  sign/encrypt/decrypt/verify, pinned signers, replay state, and file
  publication.
- `hrt-f2f` and `hrt-tool f2f`: key generation, passphrase and token files,
  keyring parsing, encryption and decryption, signature checks, and the
  client side of the login code exchange.
- Build and release: `scripts/`, cross builds, published archives, and their
  checksum files.
- CI workflows in `.github/workflows/`.
- Documentation that would lead a careful user into an unsafe action.

## Usually not a vulnerability

These limits are already documented. A report is still welcome if you can get
around a documented protection, not just restate the limit.

- The HTTPS login sends the account password to the server, so a compromised
  server can derive the cloud-data key. `hrt://` with web approval avoids
  this; see [Using hrt-tool](Using-hrt-tool#logging-in).
- The 60-second pause after a failed login is enforced by the client. The
  server applies its own rate limits.
- `look` and `status` print an uncalibrated baseline estimate. Questions about
  estimate accuracy are not security issues; open a normal issue.
- On Windows the vault relies on NTFS ACLs, and vault writes are not yet
  crash-durable.
- There is no protection against a compromised computer or a malicious binary.

See [Security Model](Security-Model) for the full list of goals and non-goals.

## Test safely

- Use synthetic data and test accounts only. Never access, request, or keep
  anyone else's data. If you reach real data by accident, stop, keep no copy,
  and say so in your report.
- Use a scratch configuration directory so your own vault is never involved:

  ```sh
  hrt-tool --config-dir ./scratch-config init
  hrt-tool --config-dir ./scratch-config config set server http://127.0.0.1:8787
  ```

- Generate throwaway keys for `hrt-cold` and F2F tests; never use keys that
  protect real data.
- Prefer a server you run yourself. The tracker repository runs locally with
  Docker, and its `compose.yaml` publishes port 8787 on `127.0.0.1` only; see
  its [README](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker#readme).
- On the default server (`https://hrt.mahiro.uk`) or anyone else's instance,
  use only your own test account, and run no load, brute-force, or
  denial-of-service tests.
- No social engineering of maintainers, operators, or users.
- Do not publish details before a fix is released or a disclosure date is
  agreed.

## What to include

- What is affected: the tool and version (`hrt-tool version --json`,
  `hrt-cold version`, or `hrt-f2f version`), or the commit if you built from
  source.
- How you installed it: the release archive name and whether you checked its
  checksum, or the build options you used (for example
  `HRT_TOOL_ENABLE_F2F=OFF`).
- Operating system and CPU architecture.
- Steps to reproduce with synthetic data, and the expected and observed
  behaviour.
- The attacker you assume: a malicious server, someone on the network path, a
  malicious proxy, another local user, or a crafted input file.
- The impact: what the attacker can read, change, or make the tool do.

`--debug` and `--trace` redact headers, but read the output before you attach
it and remove anything personal. If a log matters, reproduce the problem with a
scratch vault and share the decrypted, reviewed text, never the vault or its
password.

## What happens next

- We aim to acknowledge a report within 7 days.
- A private report is visible only to you, the maintainers, and anyone they add
  to the advisory.
- Confirmed problems are fixed with a regression test and released. A GitHub
  security advisory is published once users can update, with credit unless you
  prefer to stay anonymous.
- `0.1.x` is an experimental pre-release line: fixes are best effort, and only
  the latest release is fixed.
- Reports can be written in English or Chinese.
- There is no bug bounty.

## Canonical sources

- `SECURITY.md`
- [Tracker repository `SECURITY.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker/blob/main/SECURITY.md)
- `tools/hrt-tool/README.md` (security model)
