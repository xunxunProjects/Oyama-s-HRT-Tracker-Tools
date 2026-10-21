# Quick Start

## hrt-tool

Build and install from a checkout:

```sh
make -C tools/hrt-tool install     # PREFIX=/usr/local by default
```

Create the encrypted vault once:

```sh
hrt-tool init
```

Start the console (or run `hrt-tool` with no arguments on a terminal):

```sh
hrt-tool console
```

```text
hrt> add-link hrt://hrt.example.org:9443 mtf
hrt> commit-server mtf
hrt$mtf> status all
hrt$mtf> add dose
hrt$mtf> exit
```

`status all` shows the current estimate, the expected values at 1m–2d, and
recent blood tests. `add dose` asks for the drug, timing, and route, then saves
through the selected link. `exit` returns to the base prompt.

Other useful commands:

```sh
hrt-tool doctor                 # check the vault, proxy, trust pins, and versions
hrt-tool proxy tor              # SOCKS5 to 127.0.0.1:9050, DNS at the proxy
hrt-tool proxy i2p              # SOCKS5 to 127.0.0.1:4447
hrt-tool help exit-codes        # stable exit-code table
```

## hrt-cold

Keep the passphrase in a mode-`0600` file, offline:

```sh
hrt-cold keygen \
  --private-out backup-private.asc \
  --public-out backup-public.asc \
  --passphrase-file passphrase.txt

hrt-cold encrypt \
  --recipient backup-public.asc \
  --in complete-backup.json \
  --out complete-backup.hrtb

hrt-cold decrypt \
  --private backup-private.asc \
  --passphrase-file passphrase.txt \
  --in complete-backup.hrtb \
  --out restored-complete-backup.json
```

A failed decrypt, integrity check, signature check, or replay check writes no
plaintext output. See [Using hrt-cold](Using-hrt-cold) for limits and options.

## F2F keyring

```sh
hrt-tool f2f keygen --name 'Alice' --email 'alice@example.invalid' \
  --passphrase-file f2f.pass \
  --private-out alice-private.asc --public-out alice-public.asc
```

Verify member fingerprints in person. See [F2F Keyrings](F2F-Keyrings).

## Next

- [Build and Install](Build-and-Install)
- [Using hrt-tool](Using-hrt-tool)
- [Security Model](Security-Model)
- [Downloads and Verification](Downloads-and-Verification)
