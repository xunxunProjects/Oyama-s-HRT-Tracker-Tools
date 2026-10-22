# Who are you?

The Tools repository ships the command-line clients for the tracker. Pick the
row that matches what you need.

| You are... | Start here | Version to use |
| --- | --- | --- |
| **I use `hrt-tool`** — vault, console, remote sync, F2F | [Quick Start](Quick-Start) and [Using hrt-tool](Using-hrt-tool) | Current pre-release, `0.1.1` |
| **I need the commands** — flags, subcommands, exit codes | [CLI Command Reference](CLI-Command-Reference) | `0.1.1` |
| **I make cold backups** — OpenPGP, offline | [Using hrt-cold](Using-hrt-cold) | `v0.1.0` release |
| **I develop the tools** — C++, Rust, Go, packaging | [Developer API](Developer-API) and [Development Guide](Development-Guide) | `main` |
| **I test their security** — code review, local lab | [Security Researchers](Security-Researchers) | **Latest commit on `main`** |
| **I am an AI or a contributor** — commits and wiki pages | [AI and Contributors](AI-Contributors) | Branch + pull request |

> **Pre-release `0.1.1`.** Experimental, not independently audited, and not
> production-supported. Use synthetic data when testing. Keep private keys and
> passphrases offline and separate from backups.

For production, use a published release asset and verify its checksum. Do not
build a release from `main` for someone else's data. Security work is the
exception: start from the latest commit so you see the newest fixes.

## What is here

| Tool | Purpose |
| --- | --- |
| [`hrt-tool`](Using-hrt-tool) | Terminal client: encrypted vault, console, named links, PK status, live sync, and the F2F keyring |
| [`hrt-cold`](Using-hrt-cold) | Offline OpenPGP cold-backup CLI for the complete-backup bundle |
| [`hrt-tool f2f`](F2F-Keyrings) | Member-owned OpenPGP keys and cohort keyrings; private keys never leave the machine |

The Oyama HRT Tracker website is a compatible producer and consumer of the
bundle format. It does not handle OpenPGP keys or passphrases.

## Start here

- [Quick Start](Quick-Start) — install, first vault, console, and a cold-backup round trip.
- [Build and Install](Build-and-Install) — dependencies and CMake/Make targets.
- [Downloads and Verification](Downloads-and-Verification) — release assets and checksums.
- [Using hrt-tool](Using-hrt-tool) — servers, trust, login, live and local data, export, links, and diagnostics.
- [CLI Command Reference](CLI-Command-Reference) — every command, global flag, and exit code.
- [Using hrt-cold](Using-hrt-cold) — keygen, encrypt, decrypt, and limits.
- [Security Model](Security-Model) — what the tools protect and what they do not.
- [Troubleshooting](Troubleshooting) — exit codes, common errors, and failures.
- [Reporting Vulnerabilities](Reporting-Vulnerabilities) — where to report and how to test safely.

## Repository

- Source: <https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools>
- Releases: <https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/releases>
- Security reports: [Reporting Vulnerabilities](Reporting-Vulnerabilities) and [`SECURITY.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/blob/main/SECURITY.md)
- Contributing: [`CONTRIBUTING.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/blob/main/CONTRIBUTING.md)
- Code of conduct: [`CODE_OF_CONDUCT.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/blob/main/CODE_OF_CONDUCT.md)

The pages in this wiki are versioned in the repository under `wiki/` and
published with `scripts/publish-wiki.sh`.
