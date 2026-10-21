# Release Process

## Version source

`HRT_TOOL_VERSION` in `tools/hrt-tool/CMakeLists.txt` is the release version.
CMake injects it into:

- the C++ tool version and HTTP User-Agent,
- the C core version string,
- the Rust printer (`HRTPRINT_VERSION`),
- the Go F2F build version (`-ldflags -X main.buildVersion`).

`hrtcore` ABI version `1` does not change with a release.

`hrt-cold` and the `hrtcold` native scaffold have their own `0.1.x` line and
are not bumped by an `hrt-tool` release.

## Pre-release checks

```sh
cmake -S tools/hrt-tool -B build-verify -DCMAKE_BUILD_TYPE=Release \
  -DHRT_TOOL_VERSION=0.1.1
cmake --build build-verify --parallel
ctest --test-dir build-verify --output-on-failure
./build-verify/hrt-tool version
./build-verify/hrt-tool version --json

cd tools/hrt-f2f && go test -count=1 ./...
```

`hrt-tool version` must report the same version for `hrt-tool`, `hrtcore`,
`hrtprint`, and `f2f`.

## Build artifacts

```sh
tools/hrt-tool/scripts/build-release.sh 0.1.1 dist --target linux/amd64
tools/hrt-tool/scripts/build-release.sh 0.1.1 dist --target linux/386
tools/hrt-tool/scripts/build-release.sh 0.1.1 dist --target windows/amd64
tools/hrt-tool/scripts/build-release.sh 0.1.1 dist --target windows/386
```

Each package contains:

```text
hrt-tool-<version>-<os>-<arch>/
  bin/hrt-tool[.exe]
  LICENSE
  THIRD_PARTY_NOTICES.md
  BUILD_INFO.txt
  SHA512SUMS
  MD5SUMS
```

The script also writes `.tar.xz`, `.tar.xz.sha512`, and `.tar.xz.md5`.

Cross builds require the dependencies in [Cross Compilation](Cross-Compilation).
A target is published only after the binary exists and its `version` output has
been verified (natively, under Wine, or explicitly marked "built, not
executed").

## Signing handoff

The build does not create `.sig` files. After verifying the artifacts, the
release owner signs them:

```sh
gpg --armor --detach-sign --output hrt-tool-0.1.1-linux-amd64.tar.xz.sig \
  hrt-tool-0.1.1-linux-amd64.tar.xz
gpg --armor --detach-sign --output bin/hrt-tool.sig bin/hrt-tool
```

Verification:

```sh
sha512sum -c SHA512SUMS
md5sum -c MD5SUMS
gpg --verify hrt-tool-0.1.1-linux-amd64.tar.xz.sig hrt-tool-0.1.1-linux-amd64.tar.xz
```

SHA-512 and MD5 detect corruption. Only the GPG signature establishes who
built the artifact. MD5 is included for compatibility, not as an authenticity
claim.

## Checklist

- [ ] Version is consistent across tool, core, printer, and F2F.
- [ ] CTest and Go tests pass.
- [ ] Four target artifacts exist, or unbuilt targets are marked pending.
- [ ] Each package has `SHA512SUMS` and `MD5SUMS`.
- [ ] Each binary reports the expected version.
- [ ] Wiki downloads page lists only verified artifacts.
- [ ] GPG `.sig` files are added by the release owner.
- [ ] No real health data, keys, or secrets are in the artifacts.

## Canonical sources

- `tools/hrt-tool/scripts/build-release.sh`
- `CHANGELOG.md`
- `SECURITY.md`
