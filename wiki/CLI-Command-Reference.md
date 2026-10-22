# hrt-tool Command Reference

`hrt-tool` is the terminal client for Oyama's HRT Tracker. It keeps an
encrypted local vault, talks to the same Worker API as the website, and can
also work from a local snapshot. This page is the command index; the
[reference](hrt-tool-Reference) covers global flags, configuration keys, time
input, and file locations.

Status: **0.1.1, pre-release**. Treat it as experimental. Test with synthetic
data, keep passphrases and keys offline, and read the
[security model](Security-Model) before using a live account.

## Syntax

```text
hrt-tool [global flags] <command> [command flags]
```

Global flags may appear before or after the command. `hrt-tool help` prints the
short command list; `hrt-tool help <command>` prints a usage line;
`hrt-tool help exit-codes` prints the exit-code table.

## Global flags

| Group | Flags |
| --- | --- |
| Server and network | `--server URL`, `--proxy URL`, `--timeout SECONDS`, `--transport auto\|tls\|openssl-v1`, `--require-hrt`, `--insecure-tls`, `--trust-server` |
| Vault and identity | `--config-dir DIR`, `--vault-password-file FILE`, `--vault-password-stdin`, `--user-id ID`, `--account-password-file FILE`, `--account-password-stdin`, `--device-name NAME` |
| Data and mode | `--snapshot FILE`, `--mode transfem\|transmasc`, `--utc`, `--compute local\|server`, `--no-cache-cloud-key` |
| Output and behaviour | `--json`, `--no-color`, `--debug`, `--trace`, `--log-file FILE`, `--yes`, `--force`, `--probe`, `--no-humor` |

`--vault-password-file` and `--account-password-file` must be mode `0600`.
`--json` keeps stdout machine-readable; prompts and diagnostics go to stderr.

## Commands

### Vault and account

| Command | What it does |
| --- | --- |
| `init [--reset]` | Create the encrypted local vault. `--reset` replaces it. |
| `login [--username USER]` | Log in and store the session. HTTPS uses the account password; `hrt://` uses web approval and a device key. |
| `logout` | Revoke the current session and forget the token. |
| `whoami` | Show the current account. |
| `add-link URL NAME` | Save a named server/link context (`https://`, `hrt://`, or policy-limited `http://`). |
| `commit-server NAME` | Select a saved link for later commands; `-` selects the base link. |
| `link list` | List links, active state, mode, and login state. |
| `link show NAME` | Show one link with secrets redacted. |
| `link rename OLD NEW` | Rename a link. |
| `link remove NAME [--yes] [--force]` | Remove a link. |
| `config get [KEY]` | Show server, proxy, transport, compute, language, and related values. |
| `config set KEY VALUE` | Set a configuration value. Changing the server origin clears that origin's token and device identity. |
| `config unset KEY` | Clear a configuration value. |
| `config path` | Print the config directory and vault path. |
| `proxy tor\|i2p\|off` | Set the built-in Tor or I2P SOCKS endpoint, or clear it. |
| `proxy status\|test` | Show the configured proxy or test it. |
| `doctor` | Check vault, permissions, proxy, server policy, trust pins, and versions. |
| `version [--json]` | Print tool, core, printer, and F2F versions. |
| `completion bash\|zsh\|fish` | Print a static completion script. |

### Reading data

| Command | What it does |
| --- | --- |
| `look [--all] [--at OFFSETS] [--labs N] [--json]` | Show concentrations and recent labs. Without `--snapshot`, pulls and decrypts the newest cloud revision in memory. |
| `status [all] [--at LIST] [--labs N] [--watch SECONDS] [--json]` | Current and expected concentrations plus recent labs. `all` includes every offset. |
| `snapshot pull --out FILE [--id ID] [--force]` | Download a revision as an encrypted bundle. Refuses to overwrite unless forced. |
| `snapshot list` | List stored revisions. |
| `logs show --log-file FILE` | Decrypt an encrypted local log file. |

### Writing data

| Command | What it does |
| --- | --- |
| `add --drug NAME --time WHEN --measurement DOSE [--route ROUTE] [--mode MODE]` | Add a dose. Without `--snapshot`, pulls, edits, merges, and pushes with a conflict retry. |
| `dose list` | List doses. |
| `dose edit --id ID [--measurement DOSE] [--time WHEN] [--route ROUTE]` | Edit a dose. |
| `dose delete --id ID` | Delete a dose. |
| `lab add --time WHEN --value N --unit UNIT --assay ASSAY` | Add a lab result. |
| `lab list` | List lab results. |
| `lab edit --id ID [--value N] [--unit UNIT] [--assay ASSAY] [--time WHEN]` | Edit a lab result. |
| `lab delete --id ID` | Delete a lab result. |
| `weight get` | Print the current weight. |
| `weight set --kg N` | Set the current weight. |
| `export --out FILE\|-` | Export plaintext JSON to stdout (`-`) or an encrypted `HRTE` file. |
| `import --in FILE [--replace --yes]` | Import a sync payload or complete-backup envelope. The default merges; `--replace` needs `--yes`. |
| `snapshot push --in FILE` | Merge and upload a local snapshot. |

### Interactive and F2F

| Command | What it does |
| --- | --- |
| `console` | Interactive `hrt>` / `hrt$link>` console. |
| `f2f keygen --name NAME --email EMAIL --passphrase-file FILE --private-out FILE --public-out FILE [--force]` | Create an OpenPGP key pair. The private key stays local. |
| `f2f login --server URL (--code CODE \| --code-file FILE) --token-out FILE [--proxy SOCKS5H_URL] [--force]` | Exchange a web approval code for an F2F token. |
| `f2f register --server URL --token-file FILE --public-key FILE [--fingerprint FPR] [--proxy SOCKS5H_URL] [--json]` | Register a public key. |
| `f2f status --server URL --token-file FILE [--proxy SOCKS5H_URL] [--json]` | Show F2F registration status. |
| `f2f keyring list FILE` | List a keyring. |
| `f2f keyring merge --out FILE FILE...` | Merge keyrings. |
| `f2f keyring pull --server URL --token-file FILE --out FILE [--proxy SOCKS5H_URL] [--force]` | Pull the cohort keyring. |
| `f2f encrypt --recipient FILE [--recipient FILE ...] [--escrow FILE] [--signer-private FILE --signer-passphrase-file FILE] --in FILE --out FILE [--binary] [--force]` | Encrypt a file to recipients. |
| `f2f decrypt --private FILE --passphrase-file FILE [--verify-key FILE] --in FILE --out FILE [--force]` | Decrypt and optionally verify a signature. |
| `f2f version` | Print the F2F version. |

## Common workflows

Local snapshot (no server):

```sh
hrt-tool look --snapshot snapshot.json --user-id ID \
  --account-password-file account.txt

hrt-tool add --snapshot snapshot.json --user-id ID \
  --account-password-file account.txt \
  --drug "Estradiol valerate" --time now --measurement 2mg

hrt-tool export --out - --snapshot snapshot.json --user-id ID \
  --account-password-file account.txt
```

Remote HTTPS:

```sh
hrt-tool login --username alice --account-password-file account.txt
hrt-tool look
hrt-tool add --drug "Estradiol valerate" --time now --measurement 2mg \
  --route injection --mode transfem
hrt-tool snapshot pull --out snapshot.json
hrt-tool snapshot push --in snapshot.json
hrt-tool logout
```

Web-approved raw transport:

```sh
hrt-tool config set server hrt://hrt.example.org:9443
hrt-tool login --username alice
```

The CLI prints a 128-hex code. Approve it in the web account page, then enter
the local account password. Only the code's SHA-256 is sent.

## Exit codes

| Code | Name | Meaning |
| --- | --- | --- |
| 0 | OK | Success. |
| 1 | INTERNAL | Unexpected internal error. |
| 2 | USAGE | Invalid command line or input value. |
| 3 | VAULT | Local vault access or state error. |
| 4 | NETWORK | Transport or remote failure. |
| 5 | DATA | Malformed or inconsistent data. |
| 6 | NOT_IMPLEMENTED | Reserved or unimplemented feature. |
| 7 | AUTH | Server authentication or authorization failure. |
| 8 | TRUST | Security-policy refusal. |
| 9 | PERMISSION | Local file permission or overwrite refusal. |
| 10 | CONFLICT | Concurrent modification or duplicate. |
| 11 | CANCELLED | The user declined or cancelled. |
| 12 | CONFIG | Configuration key or value error. |
| 13 | F2F | F2F library error. |
| 14 | PLATFORM | Feature unavailable in this build. |

## Safety notes

- Plaintext is written only to stdout. Local state is encrypted at rest.
- A token is sent only to the origin saved with it. A different `--server`
  is refused with `SERVER_MISMATCH`; log in again to switch.
- The CLI does not follow HTTP redirects, so a bearer token cannot be
  redirected to another host.
- `export --out FILE` refuses to overwrite existing files.
- `import` sanitises routes, units, weights, and record counts; unknown
  routes are dropped and counted.
- Use `--no-color` for plain output and `--json` for stable machine output.
