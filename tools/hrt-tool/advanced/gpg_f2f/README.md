# GPG over F2F — advanced, not implemented

This directory is a placeholder for the optional face-to-face GPG transport.
It is **not built, imported, or invoked** by `hrt-tool` in M1-M4.

## What F2F means here

Face-to-face public-key exchange: two people verify each other's OpenPGP
fingerprints in person (or over an independent channel) and pin the public
keys. A self-hosted deployment could then wrap requests and responses in
OpenPGP messages, as an alternative to relying on TLS alone.

## Why it is deferred

- The public Cloudflare Worker does not run `gpg` and the project deliberately
  avoids adding OpenPGP.js/LGPL to the website.
- A real implementation needs a self-hosted sidecar, key rotation, revocation,
  and a recovery story.
- The default TLS + authenticated `openssl-v1` envelope covers the first
  version without growing the codebase.

## Why Python

The placeholder is Python so maintainers can review the protocol shape without
building the C/C++/Rust toolchain. It must never be a hidden runtime
dependency: `hrt-tool` does not execute it.

`gpg_f2f.py` contains function signatures and documentation only. Calling any
function raises `NotImplementedError`.
