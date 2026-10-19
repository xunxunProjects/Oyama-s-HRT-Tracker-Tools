# Oyama HRT Tracker Tools

Offline tools for the `oyama-hrt-complete-backup` bundle.

**Status: pre-release `0.1.0`.** The `.hrtb` container is experimental and not
stable. Nothing here is production-approved or independently audited. Keep
private keys and passphrases offline and separate from backups.

## What this is

This is an independent project for the offline side of Oyama HRT Tracker
backups. The first tool, [`hrt-cold`](tools/hrt-cold/README.md), is a standalone
OpenPGP cold-backup CLI:

- validates the complete-backup envelope and the experimental `.hrtb`
  container;
- signs the plaintext bundle and encrypts it to a recipient public key
  (sign-then-encrypt);
- decrypts, verifies the OpenPGP integrity check, and verifies a pinned signer
  public key;
- keeps a local high-water mark for verified backups and refuses an older
  replay by default;
- writes files atomically without replacing an existing output, and writes no
  plaintext on a failed check.

The Oyama HRT Tracker website is one compatible producer and consumer of the
bundle format. It does not handle OpenPGP keys or passphrases, and its direct
plaintext import path is unauthenticated by design.

The second tool, [`hrt-tool`](tools/hrt-tool/README.md), is the terminal
client. M2 provides the encrypted local vault, master-password gate, encrypted
logs, URL/trust policy, the C PK engine validated against the website, cloud
snapshot decryption, and local `look`/`add`/`dose`/`lab`/`weight`/`export`/
`import` commands. The live server client is M3/M4.

## Download

See the [latest release](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/releases).
The `0.1.0` pre-release publishes four binaries, a detached GPG signature for
each, and `SHA512SUMS` / `MD5SUMS`:

- `hrt-cold-linux-amd64`
- `hrt-cold-windows-amd64.exe`
- `hrt-cold-linux-386`
- `hrt-cold-windows-386.exe`

Release signing key:

```text
77E9454AEC3265F44DC9F633642B427879C25A49
haiyanfurry@proton.me
```

Verify before running:

```sh
sha512sum -c SHA512SUMS
md5sum -c MD5SUMS
gpg --verify hrt-cold-linux-amd64.sig hrt-cold-linux-amd64
```

The `.sig` files are binary detached signatures. The release page has the
commands for every asset.

## Quick start

```sh
# Keep the passphrase in a mode-0600 file, offline.
hrt-cold keygen \
  --private-out backup-private.asc \
  --public-out backup-public.asc \
  --passphrase-file passphrase.txt

hrt-cold encrypt \
  --recipient backup-public.asc \
  --signer-private signing-private.asc \
  --signer-passphrase-file signing-passphrase.txt \
  --in complete-backup.json \
  --out complete-backup.hrtb

hrt-cold decrypt \
  --private backup-private.asc \
  --passphrase-file passphrase.txt \
  --verify-key signing-public.asc \
  --in complete-backup.hrtb \
  --out restored-complete-backup.json
```

Run `hrt-cold version` to confirm the binary, Go runtime, and platform. See
[`tools/hrt-cold/README.md`](tools/hrt-cold/README.md) for all commands, limits,
and failure behaviour.

## Security model

- Encryption is public-key OpenPGP to the supplied recipient. Signing is
  optional; without `--verify-key`, decrypt does not verify origin.
- A pinned verification key fails closed on unsigned input, a bad signature, or
  a different signer.
- Anti-replay is a local high-water mark, not a global freshness service. A
  fresh device, a deleted state file, a compromised signer key, or a malicious
  binary can defeat it. `--allow-older` overrides without lowering the mark.
- A failed decrypt, integrity check, signature check, or replay check writes no
  plaintext output.
- The website's direct plaintext import is not authenticated. Website-level
  provenance would require changing the website's no-keys boundary.
- There is no key rotation, revocation workflow, recovery kit, physical erasure
  claim, or independent cryptographic audit.

## Platform support

| Platform | Evidence |
| --- | --- |
| Linux amd64 | native build and full synthetic round-trip; CI |
| Linux 386 | native build and full synthetic round-trip; CI Go tests on amd64 |
| Windows amd64 | native Go tests in CI; packaged binary round-trip under Wine |
| Windows 386 | packaged binary round-trip under Wine; native Windows 386 execution not CI-tested |

OpenPGP behaviour is delegated to gopenpgp/go-crypto. The project does not
implement its own cryptography or KDF.

## Repository layout

```text
tools/hrt-cold/      Go CLI, tests, fixtures, notices, and release script
tools/hrt-tool/      C/C++/Rust terminal client (M2 local data)
docs/native/         Docs-first C/C++ native core design, ABI, build, and test plan
docs/backup-schema-audit/TOOLS_INTERFACE.md
configure, CMakeLists.txt, cmake/, include/, tests/native/
```

The Go CLI is the working implementation. The C/C++ core is Milestone 0:
`configure`, CMake, a v1 C ABI header, and design documents exist, but no native
parser, crypto backend, replay store, or file writer is implemented yet. RNP is
the leading OpenPGP backend candidate; no crypto dependency is linked.

## Build from source

```sh
cd tools/hrt-cold
go build -o hrt-cold .
go vet ./...
go test ./...
```

Native scaffold:

```sh
./configure --build-dir=build --enable-werror
cmake --build build
(cd build && ctest --output-on-failure)
```

See [`docs/native/BUILD.md`](docs/native/BUILD.md) for options, install layout,
and Windows notes.

## Release packaging

```sh
tools/hrt-cold/scripts/build-release.sh 0.1.0 /path/to/output
```

The script runs the tests, builds the four static `CGO_ENABLED=0` binaries,
writes per-binary SHA-512/MD5 sums, and creates the release archive. The
published `0.1.0` release uses per-binary GPG signatures.

## Documentation

- [`tools/hrt-cold/README.md`](tools/hrt-cold/README.md) — CLI commands and limits
- [`tools/hrt-tool/README.md`](tools/hrt-tool/README.md) — terminal client, M2 status
- [`tools/hrt-cold/HANDOFF.md`](tools/hrt-cold/HANDOFF.md) — checkpoint and evidence
- [`docs/backup-schema-audit/TOOLS_INTERFACE.md`](docs/backup-schema-audit/TOOLS_INTERFACE.md) — bundle interface
- [`docs/native/README.md`](docs/native/README.md) — native core status
- [`SECURITY.md`](SECURITY.md) — reporting and scope
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — build, test, and contribution rules

## Licence

MIT. Third-party runtime notices are in
[`tools/hrt-cold/THIRD_PARTY_NOTICES.md`](tools/hrt-cold/THIRD_PARTY_NOTICES.md).
