# F2F Keyrings

The face-to-face (F2F) commands create member-owned OpenPGP keys, upload only
public keys, download the cohort keyring, and encrypt or decrypt locally. The
implementation is Go/OpenPGP, linked into `hrt-tool` as a c-archive. The
standalone `hrt-f2f` binary takes the same subcommands and flags.

Your private key is never sent to the server. The installed surface is:

```text
hrt-tool f2f keygen|login|register|status|keyring|encrypt|decrypt|version
```

F2F has its own `--server`, `--proxy`, `--token-file`, and `--timeout`. It never
reads or inherits the account vault's link, token, proxy, or mode. `--proxy`
takes `socks5h://HOST:PORT`; `socks5://` is rewritten to `socks5h://`. F2F
`--timeout` takes 1 to 300 seconds (default 30), unlike `hrt-tool`, which
accepts 1 to 3600.

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
file ACL. A passphrase must be at least 12 bytes. The tool checks only the
length, so choose a long, unique passphrase. Never upload the private key file.

## Register and log in

Upload `alice-public.asc` and its fingerprint in **Account → F2F keyring** on
the web app, or get a one-time login code from the same page:

```sh
hrt-tool f2f login --server https://hrt.example.org \
  --code CODE --token-out token
hrt-tool f2f register --server https://hrt.example.org --token-file token \
  --public-key alice-public.asc
```

- The code is 32 hex characters, works once, and expires after 10 minutes.
  Generating a new code invalidates the previous one.
- `login` needs `--token-out` and exactly one of `--code` or `--code-file`.
  `--code-file FILE` keeps the code out of shell history; the file must be mode
  `0600` on Unix and hold a single line.
- The token file is written with mode `0600`. The token works only for the F2F
  endpoints and expires after 7 days. It appears as `hrt-f2f CLI` under
  **Account → Active Sessions**, where you can revoke it.

## Cohort keyring

```sh
hrt-tool f2f status --server https://hrt.example.org --token-file token
hrt-tool f2f keyring pull --server https://hrt.example.org --token-file token \
  --out cohort-keyring.asc
hrt-tool f2f keyring list cohort-keyring.asc
```

| Command | Result |
| --- | --- |
| `status` | Cohort and its state, seat, member count, active key, and the cohort escrow key fingerprint when there is one. Prints `not registered in an F2F cohort` if you have no cohort. |
| `keyring pull --out FILE` | The cohort keyring, saved as armored public keys. |
| `keyring list FILE` | Each fingerprint with its user IDs. |
| `keyring merge --out FILE FILE...` | One keyring from several files, without duplicates. |

## Cohort escrow keys

A server operator can turn on cohort escrow. The operator generates one
OpenPGP key pair per cohort offline and provisions it to the server. This is
the operator's key, not yours: members' own private keys are never escrowed or
uploaded. The tool uses only the escrow public key and cannot recover an
escrowed message. Ask the operator who holds the escrow private key and when it
may be used.

When a cohort has an escrow key:

- `f2f status` prints `escrow key: <fingerprint>`.
- The keyring from `keyring pull` contains the member keys **and the cohort
  escrow public key**.
- `f2f encrypt` encrypts to every key in every `--recipient` file. Encrypting to
  the pulled keyring therefore also encrypts to the escrow key, even without
  `--escrow`. Check the `encrypted for:` line before you send the file.
- `--escrow FILE` adds an escrow public key explicitly.

To encrypt for members only, build the recipient file from the members' own
public key files, for example with `keyring merge`, and compare its
fingerprints with `keyring list` and `status` first.

## Encrypt and decrypt

```sh
hrt-tool f2f encrypt --recipient cohort-keyring.asc --in message.txt --out message.asc
hrt-tool f2f decrypt --private alice-private.asc --passphrase-file f2f.pass \
  --in message.asc --out message.txt
```

- `encrypt` needs `--in`, `--out`, and at least one `--recipient` or
  `--escrow` file.
- `--recipient` is repeatable and takes a public key or a keyring file. One
  encryption accepts at most 100 keys and 1 MiB of armored key data. Both
  limits are counted across all recipient files and the `--escrow` file
  together, before duplicate keys are removed. A full 100-member cohort keyring
  plus a separate escrow key can exceed the key limit; trim the recipient set
  before encrypting.
- Expired, revoked, and non-encrypting recipient keys are refused, and one such
  key blocks the whole encryption. An empty message is refused.
- `--signer-private` and `--signer-passphrase-file` sign inside the encrypted
  message and must be given together. `decrypt --verify-key bob-public.asc`
  requires a valid signature from that key.
- `decrypt` needs `--private`, `--passphrase-file`, `--in`, and `--out`.
- `--binary` writes binary output instead of ASCII armor.
- Encryption is authenticated and uncompressed. Output files are atomic, mode
  `0600`, and refuse replacement without `--force`.

## Verify fingerprints

**Verify member fingerprints in person.** A keyring that imports cleanly is not
proof that a key belongs to the person you think it does. GnuPG can import the
same armored files if you prefer to compare fingerprints there.

## Non-goals

- No escrow or upload of members' own private keys.
- No recovery of escrowed messages by this tool.
- No message relay or storage. Send the encrypted file over any channel.
- No global identity verification or trust-on-first-use for cohort members.
- No protection against a compromised endpoint or a malicious binary.

## Canonical sources

- `tools/hrt-f2f/README.md`
- `tools/hrt-f2f/main.go`, `tools/hrt-f2f/crypto.go`
- `docs/hrt-protocol/DEV_GUIDE.md` (F2F service section)
- Main repo [`docs/hrt-protocol/F2F_SERVICE.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker/blob/main/docs/hrt-protocol/F2F_SERVICE.md)
  (server side, cohorts, escrow provisioning)
