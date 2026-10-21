# Using hrt-cold

`hrt-cold` is the offline OpenPGP cold-backup CLI. It validates the
complete-backup envelope and the experimental `.hrtb` container, signs,
encrypts, decrypts, and keeps a local anti-replay mark.

> The `.hrtb` container is experimental and not stable. `hrt-cold` is not
> production-approved or audited.

## Commands

| Command | Purpose |
| --- | --- |
| `keygen` | Create an OpenPGP key pair. |
| `encrypt` | Validate, sign, and encrypt a bundle to a recipient. |
| `decrypt` | Decrypt, verify integrity, and optionally verify a pinned signer. |
| `inspect` | Show the outer container without decrypting. |
| `version` | Print the build, Go runtime, and platform. |

## Round trip

```sh
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

Keep the passphrase file mode `0600` and offline. Output files are written
atomically with mode `0600` and never replace an existing file unless `--force`
is given.

## Security limits

- Encryption is public-key OpenPGP to the supplied recipient. Signing is
  optional; without `--verify-key`, decrypt does not verify origin.
- A pinned verification key fails closed on unsigned input, a bad signature,
  or a different signer.
- Anti-replay is a local high-water mark. A fresh device, a deleted state file,
  a compromised signer key, or a malicious binary can defeat it.
  `--allow-older` overrides without lowering the mark.
- A failed decrypt, integrity check, signature check, or replay check writes no
  plaintext output.
- `inspect` reads the outer container only; it is not recipient proof or
  integrity proof. Only a successful `decrypt` demonstrates those.
- There is no key rotation, revocation workflow, recovery kit, physical
  erasure claim, or independent cryptographic audit.

The website's direct plaintext import is unauthenticated by design and never
handles OpenPGP keys.

## Canonical sources

- `tools/hrt-cold/README.md`
- `docs/backup-schema-audit/TOOLS_INTERFACE.md`
- `tools/hrt-cold/HANDOFF.md`
