# F2F Keyrings

The face-to-face (F2F) commands create member-owned OpenPGP keys, upload only
public keys, download the cohort keyring, and encrypt or decrypt locally. The
implementation is Go/OpenPGP, linked into `hrt-tool` as a c-archive.

Private keys are never sent to the server. The installed surface is:

```text
hrt-tool f2f keygen|login|register|status|keyring|encrypt|decrypt|version
```

F2F has its own `--server`, `--proxy`, and `--token-file`. It never reads or
inherits the account vault's link, token, proxy, or mode.

## Key generation

```sh
printf '%s\n' 'a long unique passphrase' > f2f.pass
chmod 600 f2f.pass

hrt-tool f2f keygen \
  --name 'Alice' --email 'alice@example.invalid' \
  --passphrase-file f2f.pass \
  --private-out alice-private.asc --public-out alice-public.asc
```

Passphrase and token files must be mode `0600` on Unix. Windows relies on the
file ACL.

## Register and log in

Upload `alice-public.asc` and its fingerprint in **Account → F2F keyring** on
the web app, or use a one-time login code from the same page:

```sh
hrt-tool f2f login --server https://hrt.example.org \
  --code CODE --token-out token
hrt-tool f2f register --server https://hrt.example.org --token-file token \
  --public-key alice-public.asc
```

`--code-file FILE` can replace `--code CODE`. Then fetch the cohort keyring:

```sh
hrt-tool f2f status --server https://hrt.example.org --token-file token
hrt-tool f2f keyring pull --server https://hrt.example.org --token-file token \
  --out cohort-keyring.asc
```

## Encrypt and decrypt

```sh
hrt-tool f2f encrypt --recipient cohort-keyring.asc --in message.txt --out message.asc
hrt-tool f2f decrypt --private alice-private.asc --passphrase-file f2f.pass \
  --in message.asc --out message.txt
```

`--recipient` is repeatable. Use `--signer-private` and
`--signer-passphrase-file` to sign inside the encrypted message.

Encryption is authenticated and uncompressed. Signing is optional and verifies
the plaintext inside the encrypted message. Output files are atomic, mode
`0600`, and refuse replacement without `--force`.

## Verify fingerprints

**Verify member fingerprints in person.** A keyring that imports cleanly is not
proof that a key belongs to the person you think it does. GnuPG can import the
same armored files if you prefer to compare fingerprints there.

## Non-goals

- No private-key escrow or server-side key handling.
- No global identity verification or trust-on-first-use for cohort members.
- No protection against a compromised endpoint or a malicious binary.

## Canonical sources

- `tools/hrt-f2f/README.md`
- `docs/hrt-protocol/DEV_GUIDE.md` (F2F service section)
