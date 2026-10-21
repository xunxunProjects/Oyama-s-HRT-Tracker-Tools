# Oyama HRT Tracker Tools

Offline command-line tools for the Oyama HRT Tracker data format: `hrt-tool`
(encrypted vault, console, PK status, live sync, and the F2F keyring) and
`hrt-cold` (OpenPGP cold backups).

> **Pre-release `0.1.1`.** Experimental, not independently audited, and not
> production-supported. Use synthetic data when testing. Keep private keys and
> passphrases offline.

## Quick Start

```sh
make -C tools/hrt-tool install   # PREFIX=/usr/local by default
hrt-tool init
hrt-tool console
```

```text
hrt> add-link hrt://hrt.example.org:9443 mtf
hrt> commit-server mtf
hrt$mtf> status all
hrt$mtf> add dose
```

## Essential Documentation

| Need | Read |
| --- | --- |
| Install, first vault, cold-backup round trip | [Quick Start](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Quick-Start) |
| Build from source, install, update | [Build and Install](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Build-and-Install) |
| Commands and console | [Using hrt-tool](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Using-hrt-tool) |
| Offline backups | [Using hrt-cold](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Using-hrt-cold) |
| F2F keys and cohorts | [F2F Keyrings](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/F2F-Keyrings) |
| Security and privacy | [Security Model](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Security-Model), [`SECURITY.md`](SECURITY.md) |
| Download and verify | [Downloads and Verification](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Downloads-and-Verification) |
| Develop or package | [Development Guide](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Development-Guide), [`CONTRIBUTING.md`](CONTRIBUTING.md) |
| Errors and exit codes | [Troubleshooting](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Troubleshooting) |

## Who Are You

- **I use the website and want a terminal client.** Start with [Using hrt-tool](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Using-hrt-tool).
- **I want encrypted offline backups.** Start with [Using hrt-cold](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Using-hrt-cold).
- **I am in an F2F cohort.** Start with [F2F Keyrings](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/F2F-Keyrings).
- **I need Tor, I2P, Qubes, Whonix, or Tails.** Start with [Privacy Environments](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Privacy-Environments).
- **I build, package, or maintain releases.** Start with [Build and Install](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Build-and-Install) and [Cross Compilation](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Cross-Compilation).
- **I review security.** Read [Security Model](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Security-Model) and [`SECURITY.md`](SECURITY.md) before testing.

## For Specific Users

- **hrt-cold only:** the offline OpenPGP backup CLI; it never talks to the server.
- **hrt-tool users:** one binary for the console, named links, local data, live sync, and `hrt-tool f2f ...`.
- **Windows and 32-bit users:** check [Cross Compilation](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Cross-Compilation) and the release notes; do not assume an artifact exists until it is listed.
- **Qubes, Whonix, Tails, Tor, I2P:** see [Privacy Environments](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Privacy-Environments).
- **Release maintainers and signers:** see [Release Process](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Release-Process).
- **Distro packagers:** see [Build and Install](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/wiki/Build-and-Install); runtime dependencies are OpenSSL 3, libcurl, and the platform C++ runtime.

MIT licensed. Third-party notices are included in each release package.
