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

For `0.1.1` the following were run:

- `linux/amd64`: native build, full CTest suite (40/40), `hrt-tool version`,
  SHA-512 and MD5 verification.
- `linux/386`: 32-bit build, `hrt-tool version` run natively on the x86_64
  host with 32-bit libraries, SHA-512 and MD5 verification.
- `windows/amd64`, `windows/386`: static MinGW builds, `hrt-tool version` run
  under Wine, SHA-512 and MD5 verification. The executables import only
  Windows system DLLs; no libcurl or OpenSSL DLL is needed.

Native Windows execution is separate evidence. An artifact that exists
without a recorded version check is marked "built, not executed", not
"verified".

## Canonical sources

- `tools/hrt-tool/scripts/build-release.sh`
- `CHANGELOG.md`
- `.github/workflows/`
