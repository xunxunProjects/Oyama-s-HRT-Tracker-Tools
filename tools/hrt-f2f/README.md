# hrt-f2f

A dedicated command-line client for the face-to-face (F2F) OpenPGP keyring.
It generates keys, encrypts to a cohort's members (and optional escrow key),
and decrypts locally. It is separate from `hrt-cold` (offline backups) and
`hrt-tool` (account transport); it never uploads a private key.

## Security model

- Private keys are created locally and stored only in the files you choose.
  The F2F server receives the armored **public** key and its fingerprint.
- Passphrase files and token files must be mode `0600` on Unix. Windows relies
  on the file ACL; the mode check is skipped there.
- Output files are written atomically with mode `0600` and refuse to replace
  an existing file unless `--force` is given.
- The network client verifies the server certificate normally. When a proxy
  is configured, only `socks5h://` is accepted; the proxy resolves every name
  and environment proxy variables are ignored.
- Encryption uses authenticated encryption with no compression. Signing is
  optional and verifies the plaintext inside the encrypted message.
- Verify member fingerprints **in person**. A keyring that imports cleanly is
  not proof that a key belongs to the person you think it does.

## Build

```sh
cd tools/hrt-f2f
go build -trimpath -o hrt-f2f .
```

Cross-compile in the usual Go way, for example
`GOOS=windows GOARCH=amd64 go build -o hrt-f2f.exe .`.

## Quick start

Generate a key pair (the passphrase file must be mode 0600 and at least 12
bytes):

```sh
printf '%s\n' 'a long unique passphrase' > f2f.pass
chmod 600 f2f.pass
hrt-f2f keygen \
  --name 'Alice' --email 'alice@example.invalid' \
  --passphrase-file f2f.pass \
  --private-out alice-private.asc --public-out alice-public.asc
```

Upload `alice-public.asc` and its fingerprint in **Account → F2F keyring** on
the web app, or from the CLI with a web session token:

```sh
# The token is the web session JWT. Treat it like a password and keep it 0600.
printf '%s\n' "$SESSION_TOKEN" > f2f.token
chmod 600 f2f.token
hrt-f2f register --server https://hrt.example.com --token-file f2f.token \
  --public-key alice-public.asc
```

Download the cohort keyring, import it into your own tools, and encrypt:

```sh
hrt-f2f keyring pull --server https://hrt.example.com --token-file f2f.token \
  --out cohort.asc
hrt-f2f keyring list cohort.asc

hrt-f2f encrypt --recipient cohort.asc --in message.txt --out message.asc
```

If the cohort has an escrow key, add it explicitly:

```sh
hrt-f2f encrypt --recipient cohort.asc --escrow escrow-public.asc \
  --in message.txt --out message.asc
```

Decrypt with your private key:

```sh
hrt-f2f decrypt --private alice-private.asc --passphrase-file f2f.pass \
  --in message.asc --out message.txt
```

To require a signature from a known member:

```sh
hrt-f2f decrypt --private alice-private.asc --passphrase-file f2f.pass \
  --verify-key bob-public.asc --in message.asc --out message.txt
```

## Commands

| Command | Purpose |
| --- | --- |
| `keygen` | Generate a passphrase-locked OpenPGP key pair. |
| `register` | Upload a public key and fingerprint to the F2F API. |
| `status` | Show the cohort, seat, active key, and escrow fingerprint. |
| `keyring list` | List fingerprints and user IDs in a local keyring. |
| `keyring merge` | Merge public-key files, removing duplicates. |
| `keyring pull` | Download the member-only cohort keyring. |
| `encrypt` | Encrypt to one or more recipients, optionally sign. |
| `decrypt` | Decrypt, optionally requiring a signer. |
| `version` | Print the version. |

Run any command with `--help` for its flags. Network commands accept
`--server URL`, `--token-file FILE`, `--proxy socks5h://HOST:PORT`, and
`--timeout SECONDS` where applicable.

## Standard GnuPG

The keyring is ordinary ASCII-armored OpenPGP, so GnuPG works too:

```sh
gpg --import cohort.asc
gpg --fingerprint
gpg --encrypt --armor --recipient MEMBER_FPR --recipient ESCROW_FPR \
  --output message.asc message.txt
gpg --decrypt message.asc > message.txt
```

The test suite verifies both directions: GnuPG decrypts output produced by
this CLI, and the CLI reads keys and messages produced by GnuPG.

## Limits and non-goals

- This tool does not run a message relay and does not store messages on the
  server. Members exchange ciphertext through the channel they choose.
- Escrow keys are provisioned by the server operator. This tool only uses the
  escrow public key; it cannot recover an escrowed message.
- The web session token is a credential. A future enrollment flow may replace
  the manual token file; until then, do not put the token in shell history or
  command-line arguments.
- The passphrase check is a length check, not an entropy estimator. Use a
  long, unique passphrase or a password manager.
- The `.asc` files contain public keys unless they are named `private`; never
  upload a private-key file.

## Tests

```sh
GOPROXY=off go test -count=1 ./...
```

The suite covers key generation, locking, multi-recipient encryption, signing,
tamper rejection, keyring splitting/deduplication, file permissions, the HTTP
client, and GnuPG interoperability. GnuPG tests are skipped when `gpg` is not
installed.
