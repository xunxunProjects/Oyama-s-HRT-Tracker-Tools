# Security policy

## Supported versions

`0.1.x` is an experimental pre-release line. Security fixes are applied on a
best-effort basis to the latest release only; there is no long-term support
commitment and no production-support claim.

| Version | Supported |
| --- | --- |
| Latest `0.1.x` release | Best effort |
| Older releases | No |

## Reporting a vulnerability

Do not open a public issue, discussion, or pull request with exploit details,
real health data, private keys, passphrases, tokens, vaults, or production
backups. Do not send that material in a private report either.

Preferred: use GitHub's private vulnerability reporting for this repository
(**Security → Report a vulnerability**, or the
[advisory form](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/security/advisories/new)).

If private reporting is not available, open a minimal public issue asking the
maintainers for a private channel. Do not include the vulnerability details or
sensitive material in that issue.

Problems in the web app, the Worker API (including the `/api/tool/v1/` and F2F
endpoints), the `hrt-server` raw `hrt/1` adapter, the Docker image, or the
desktop app belong to the tracker repository. Report them through its
[advisory form](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker/security/advisories/new).
If a problem spans both repositories, report it once and say so.

Include, once a private channel is established:

- the affected tool and version (`hrt-tool version --json`,
  `hrt-cold version`, or `hrt-f2f version`) or commit, the platform, and how
  it was installed or built;
- a synthetic reproducer;
- expected and observed behaviour;
- the attacker you assume (malicious server, network path, proxy, another
  local user, or a crafted input file) and the impact;
- which part is affected: `hrt-tool` (vault, server trust, transport, login,
  snapshots, export/import, logs, console), `hrt-cold` (Go CLI,
  container/bundle parser, OpenPGP integration, replay state), `hrt-f2f`,
  packaging, CI, or documentation.

The [Reporting Vulnerabilities](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Reporting-Vulnerabilities)
wiki page is a longer guide with safe-testing advice.

## What to expect

- We aim to acknowledge a report within 7 days.
- Confirmed problems are fixed with a regression test in a new release. A
  GitHub security advisory is published once users can update, with credit
  unless you prefer to stay anonymous.
- Please give us a reasonable chance to fix the problem before disclosing it
  publicly.
- Reports can be written in English or Chinese.

## Scope

In scope:

- `hrt-tool`: the encrypted vault and its file permissions, server trust and
  SPKI pinning, the trust probe and redirect handling, proxy and DNS handling
  (including Tor and I2P), the `hrt/1` client and its device key, login and
  session handling, local snapshots, HRTE export and import, encrypted logs,
  and the console;
- `hrt-cold` command behaviour and failure paths;
- `.hrtb` container and complete-backup envelope validation;
- sign/encrypt/decrypt/verify and pinned-signer behaviour;
- replay state handling and file publication;
- `hrt-f2f` and `hrt-tool f2f`: key generation, passphrase and token files,
  keyring parsing, encryption, decryption, and signature checks;
- the native scaffold, build and release scripts, release packaging,
  checksums, and signatures;
- CI workflows in `.github/workflows/`.

Out of scope here:

- the Oyama HRT Tracker website, its Workers deployment, the `hrt-server`
  adapter, the Docker image, and the desktop app (report to the tracker
  repository, see above);
- vulnerabilities in the linked libraries themselves, such as OpenPGP,
  libcurl, or OpenSSL (report upstream, and tell us if the impact reaches
  these tools);
- servers run by third parties (report to the operator, and tell us if the
  cause is in our code);
- hosting, DNS, or GitHub account security;
- findings that require real health data, real keys, or a compromised
  endpoint to demonstrate;
- documented limitations, such as the HTTPS login sending the account
  password to the server, unless a documented protection can be bypassed.

## Research rules

- Use synthetic data and test accounts only.
- Do not test against other people's accounts, backups, or devices.
- Prefer a local test instance. Do not run load, brute-force, or
  denial-of-service tests against shared infrastructure, including the
  default server.
- Do not publish exploit details before a fix or coordinated disclosure.
- No bug bounty is offered.

## No audit claim

This project is an engineering checkpoint with regression tests. It is not an
independent security audit, and the absence of a known finding does not mean
"no vulnerabilities".
