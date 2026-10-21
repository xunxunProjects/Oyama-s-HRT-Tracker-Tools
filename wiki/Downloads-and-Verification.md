# Downloads and Verification

Release artifacts are attached to
[GitHub Releases](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/releases).
The release page is authoritative: download only the files listed there.

## Expected package layout

```text
hrt-tool-<version>-<os>-<arch>/
  bin/hrt-tool[.exe]
  LICENSE
  THIRD_PARTY_NOTICES.md
  BUILD_INFO.txt
  SHA512SUMS
  MD5SUMS
```

Alongside the archive:

```text
hrt-tool-<version>-<os>-<arch>.tar.xz
hrt-tool-<version>-<os>-<arch>.tar.xz.sha512
hrt-tool-<version>-<os>-<arch>.tar.xz.md5
```

A target is listed as available only after its binary was built and its
version output was verified. Targets that could not be built are marked
pending in the release notes instead of being published.

## Verify

```sh
# Archive
sha512sum -c hrt-tool-0.1.1-linux-amd64.tar.xz.sha512
md5sum -c hrt-tool-0.1.1-linux-amd64.tar.xz.md5

# Binary inside the extracted package
cd hrt-tool-0.1.1-linux-amd64
sha512sum -c SHA512SUMS
md5sum -c MD5SUMS

# Detached GPG signature, once the release owner publishes it
gpg --verify hrt-tool-0.1.1-linux-amd64.tar.xz.sig hrt-tool-0.1.1-linux-amd64.tar.xz
```

Check that the GPG fingerprint matches the fingerprint in that release's notes.
Do not assume a key from an older release signed a newer one.

## What each check means

| Check | Detects | Does not prove |
| --- | --- | --- |
| SHA-512 | Corruption or a truncated download | Who built it |
| MD5 | Compatibility checksum; also corruption | Authenticity; MD5 is not collision-resistant |
| GPG `.sig` | Who signed the artifact | That the binary is free of bugs |

## Platform evidence

The release notes record what was actually run for each target:

- `linux/amd64`: native build, full test suite, `hrt-tool version`.
- `linux/386`: built with 32-bit libraries; run natively when available.
- `windows/amd64`, `windows/386`: built with MinGW; `hrt-tool version` under
  Wine. Native Windows execution is separate evidence.

An artifact that exists without a recorded version check is marked
"built, not executed", not "verified".

## Canonical sources

- `tools/hrt-tool/scripts/build-release.sh`
- `CHANGELOG.md`
- `.github/workflows/`
