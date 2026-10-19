"""Placeholder for the optional face-to-face GPG transport.

This module is documentation, not a working implementation. ``hrt-tool`` does
not import or execute it, and it is not part of the M1-M4 build.

F2F here means face-to-face public-key exchange: the two people verify each
other's OpenPGP fingerprints in person (or over an independent channel) and pin
the public keys. A self-hosted sidecar could then wrap tool requests and
responses in OpenPGP messages.

A real implementation needs, at least:

* an armored public-key export and a fingerprint the user can compare;
* a pin store with a clear on-change warning;
* key rotation and revocation;
* an OpenPGP library or sidecar with an OSI-approved licence;
* tests for wrong-key, tampered, unsigned, and replayed messages.

The public Cloudflare Worker does not run ``gpg`` and does not add an OpenPGP
dependency. This file exists so maintainers can review the protocol shape
without building the C/C++/Rust toolchain. It must never become a hidden
runtime dependency.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class PeerKey:
    """A pinned peer public key."""

    fingerprint: str
    armored_public_key: str


def export_public_key(armored_private_key: str, passphrase: str) -> str:
    """Return the armored public key for ``armored_private_key``.

    The real implementation would unlock the private key in memory, derive the
    public key, and zeroize the passphrase and private key material.
    """

    raise NotImplementedError("GPG F2F is reserved for a later milestone")


def import_peer_key(armored_public_key: str, expected_fingerprint: str) -> PeerKey:
    """Verify the fingerprint and return a pinned peer key.

    The fingerprint comparison must happen out of band; the server cannot be
    the source of truth for it.
    """

    raise NotImplementedError("GPG F2F is reserved for a later milestone")


def encrypt_request(peer: PeerKey, plaintext: bytes) -> bytes:
    """Encrypt a request to a pinned peer key."""

    raise NotImplementedError("GPG F2F is reserved for a later milestone")


def decrypt_response(armored_private_key: str, passphrase: str, ciphertext: bytes) -> bytes:
    """Decrypt and verify a response from the expected peer."""

    raise NotImplementedError("GPG F2F is reserved for a later milestone")


def main() -> int:
    raise SystemExit("gpg_f2f is a documentation-only placeholder; see README.md")


if __name__ == "__main__":
    main()
