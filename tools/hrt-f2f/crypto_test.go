package main

import (
	"bytes"
	"strings"
	"testing"
)

var testPassphrase = []byte("correct horse battery staple")

func mustKey(t *testing.T, name string) *generatedKey {
	t.Helper()
	key, err := generateF2FKey(name, strings.ToLower(name)+"@example.invalid", testPassphrase)
	if err != nil {
		t.Fatalf("generate key: %v", err)
	}
	return key
}

func TestGenerateKeyProducesLockedMatchingKeyPair(t *testing.T) {
	alice := mustKey(t, "Alice")
	if !strings.HasPrefix(alice.PrivateArmor, "-----BEGIN PGP PRIVATE KEY BLOCK-----") {
		t.Fatalf("private output is not an armored private key")
	}
	if !strings.HasPrefix(alice.PublicArmor, "-----BEGIN PGP PUBLIC KEY BLOCK-----") {
		t.Fatalf("public output is not an armored public key")
	}
	if len(alice.Fingerprint) != 40 && len(alice.Fingerprint) != 64 {
		t.Fatalf("unexpected fingerprint %q", alice.Fingerprint)
	}
	if _, err := unlockPrivateKey(alice.PrivateArmor, []byte("wrong passphrase")); err == nil {
		t.Fatalf("wrong passphrase unlocked the key")
	}
	unlocked, err := unlockPrivateKey(alice.PrivateArmor, testPassphrase)
	if err != nil {
		t.Fatalf("unlock: %v", err)
	}
	defer unlocked.ClearPrivateParams()
	if canonicalFingerprint(unlocked.GetFingerprint()) != alice.Fingerprint {
		t.Fatalf("fingerprint mismatch")
	}
}

func TestRejectsShortPassphrase(t *testing.T) {
	if _, err := generateF2FKey("Alice", "alice@example.invalid", []byte("short")); err == nil {
		t.Fatalf("short passphrase was accepted")
	}
}

func TestEncryptDecryptRoundTrip(t *testing.T) {
	alice := mustKey(t, "Alice")
	plaintext := []byte("face to face message\n")
	ciphertext, recipients, signer, err := encryptF2F(
		[]string{alice.PublicArmor}, "", nil, plaintext, true,
	)
	if err != nil {
		t.Fatalf("encrypt: %v", err)
	}
	if !strings.HasPrefix(string(ciphertext), "-----BEGIN PGP MESSAGE-----") {
		t.Fatalf("output is not armored")
	}
	if len(recipients) != 1 || recipients[0] != alice.Fingerprint {
		t.Fatalf("recipient fingerprints: %v", recipients)
	}
	if signer != "" {
		t.Fatalf("unexpected signer %q", signer)
	}
	decrypted, verifiedSigner, err := decryptF2F(alice.PrivateArmor, testPassphrase, ciphertext, "", 1<<20)
	if err != nil {
		t.Fatalf("decrypt: %v", err)
	}
	if !bytes.Equal(decrypted, plaintext) {
		t.Fatalf("plaintext mismatch: %q", decrypted)
	}
	if verifiedSigner != "" {
		t.Fatalf("unexpected verified signer %q", verifiedSigner)
	}
}

func TestMultipleRecipients(t *testing.T) {
	alice := mustKey(t, "Alice")
	bob := mustKey(t, "Bob")
	charlie := mustKey(t, "Charlie")
	plaintext := []byte("members only")
	ciphertext, _, _, err := encryptF2F(
		[]string{alice.PublicArmor, bob.PublicArmor}, "", nil, plaintext, true,
	)
	if err != nil {
		t.Fatalf("encrypt: %v", err)
	}
	for name, key := range map[string]*generatedKey{"alice": alice, "bob": bob} {
		decrypted, _, err := decryptF2F(key.PrivateArmor, testPassphrase, ciphertext, "", 1<<20)
		if err != nil {
			t.Fatalf("%s decrypt: %v", name, err)
		}
		if !bytes.Equal(decrypted, plaintext) {
			t.Fatalf("%s plaintext mismatch", name)
		}
	}
	if _, _, err := decryptF2F(charlie.PrivateArmor, testPassphrase, ciphertext, "", 1<<20); err == nil {
		t.Fatalf("a non-recipient decrypted the message")
	}
}

func TestSignAndVerify(t *testing.T) {
	alice := mustKey(t, "Alice")
	bob := mustKey(t, "Bob")
	charlie := mustKey(t, "Charlie")
	plaintext := []byte("signed message")
	ciphertext, _, signer, err := encryptF2F(
		[]string{alice.PublicArmor}, bob.PrivateArmor, testPassphrase, plaintext, true,
	)
	if err != nil {
		t.Fatalf("encrypt/sign: %v", err)
	}
	if signer != bob.Fingerprint {
		t.Fatalf("signer fingerprint %q, want %q", signer, bob.Fingerprint)
	}
	decrypted, verified, err := decryptF2F(
		alice.PrivateArmor, testPassphrase, ciphertext, bob.PublicArmor, 1<<20,
	)
	if err != nil {
		t.Fatalf("decrypt/verify: %v", err)
	}
	if !bytes.Equal(decrypted, plaintext) || verified != bob.Fingerprint {
		t.Fatalf("verified signer %q, want %q", verified, bob.Fingerprint)
	}
	if _, _, err := decryptF2F(
		alice.PrivateArmor, testPassphrase, ciphertext, charlie.PublicArmor, 1<<20,
	); err == nil {
		t.Fatalf("a different verification key was accepted")
	}
	if _, _, err := decryptF2F(
		alice.PrivateArmor, testPassphrase, ciphertext, "", 1<<20,
	); err != nil {
		t.Fatalf("unsigned-intolerant decrypt failed: %v", err)
	}
}

func TestTamperedCiphertextFails(t *testing.T) {
	alice := mustKey(t, "Alice")
	ciphertext, _, _, err := encryptF2F(
		[]string{alice.PublicArmor}, "", nil, []byte("tamper me"), true,
	)
	if err != nil {
		t.Fatalf("encrypt: %v", err)
	}
	tampered := append([]byte(nil), ciphertext...)
	marker := bytes.Index(tampered, []byte("\n\n"))
	if marker < 0 || marker+10 >= len(tampered) {
		t.Fatalf("cannot find armor body")
	}
	position := marker + 10
	if tampered[position] == 'A' {
		tampered[position] = 'B'
	} else {
		tampered[position] = 'A'
	}
	if _, _, err := decryptF2F(alice.PrivateArmor, testPassphrase, tampered, "", 1<<20); err == nil {
		t.Fatalf("tampered ciphertext was accepted")
	}
}

func TestRefusesEmptyPlaintext(t *testing.T) {
	alice := mustKey(t, "Alice")
	if _, _, _, err := encryptF2F([]string{alice.PublicArmor}, "", nil, nil, true); err == nil {
		t.Fatalf("empty plaintext was encrypted")
	}
}

func TestMergeKeyrings(t *testing.T) {
	alice := mustKey(t, "Alice")
	bob := mustKey(t, "Bob")
	merged, fingerprints, err := mergeKeyrings([]string{
		alice.PublicArmor, bob.PublicArmor, alice.PublicArmor,
	})
	if err != nil {
		t.Fatalf("merge: %v", err)
	}
	if len(fingerprints) != 2 {
		t.Fatalf("fingerprints = %v", fingerprints)
	}
	ring, parsed, err := parsePublicKeyRing(merged)
	if err != nil || ring.CountEntities() != 2 || len(parsed) != 2 {
		t.Fatalf("merged keyring: %v, %d, %d", err, ring.CountEntities(), len(parsed))
	}
	infos, err := listKeyring(merged)
	if err != nil || len(infos) != 2 {
		t.Fatalf("list: %v, %d", err, len(infos))
	}
	for _, info := range infos {
		if len(info.UserIDs) != 1 || !strings.Contains(info.UserIDs[0], "example.invalid") {
			t.Fatalf("unexpected user ids: %v", info.UserIDs)
		}
	}
}

func TestParsePublicKeyRingDeduplicates(t *testing.T) {
	alice := mustKey(t, "Alice")
	bob := mustKey(t, "Bob")
	combined := alice.PublicArmor + "\n" + bob.PublicArmor + "\n" + alice.PublicArmor
	ring, fingerprints, err := parsePublicKeyRing(combined)
	if err != nil {
		t.Fatalf("parse keyring: %v", err)
	}
	if ring.CountEntities() != 2 || len(fingerprints) != 2 {
		t.Fatalf("got %d entities / %d fingerprints", ring.CountEntities(), len(fingerprints))
	}
}
