# Oyama HRT Tracker Tools

Offline command-line tools for the Oyama HRT Tracker data format.

> **Pre-release `0.1.1`.** Experimental, not independently audited, and not
> production-supported. Use synthetic data when testing. Keep private keys and
> passphrases offline and separate from backups.

## What is here

| Tool | Purpose |
| --- | --- |
| [`hrt-tool`](Using-hrt-tool) | Terminal client: encrypted vault, console, named links, PK status, live sync, and the F2F keyring. |
| [`hrt-cold`](Using-hrt-cold) | Offline OpenPGP cold-backup CLI for the complete-backup bundle. |
| [`hrt-tool f2f`](F2F-Keyrings) | Member-owned OpenPGP keys and cohort keyrings; private keys never leave the machine. |

The Oyama HRT Tracker website is a compatible producer and consumer of the
bundle format. It does not handle OpenPGP keys or passphrases.

## Start here

- [Quick Start](Quick-Start) — install, first vault, console, and a cold-backup round trip.
- [Build and Install](Build-and-Install) — dependencies and CMake/Make targets.
- [Using hrt-tool](Using-hrt-tool) — commands, links, status, and diagnostics.
- [Using hrt-cold](Using-hrt-cold) — keygen, encrypt, decrypt, and limits.
- [Security Model](Security-Model) — what the tools protect and what they do not.
- [Troubleshooting](Troubleshooting) — exit codes and common failures.

## Repository

- Source: <https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools>
- Releases: <https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/releases>
- Security reports: [`SECURITY.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/blob/main/SECURITY.md)
- Contributing: [`CONTRIBUTING.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/blob/main/CONTRIBUTING.md)

The pages in this wiki are versioned in the repository under `wiki/` and
published with `scripts/publish-wiki.sh`.
