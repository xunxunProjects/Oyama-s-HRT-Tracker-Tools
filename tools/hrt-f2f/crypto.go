package main

import (
	"bytes"
	"encoding/hex"
	"errors"
	"fmt"
	"sort"
	"strings"
	"time"

	openpgp "github.com/ProtonMail/go-crypto/openpgp/v2"
	"github.com/ProtonMail/gopenpgp/v3/constants"
	"github.com/ProtonMail/gopenpgp/v3/crypto"
)

const (
	maxRecipientKeys = 100
	maxKeyringBytes  = 1 << 20
)

type generatedKey struct {
	PrivateArmor string
	PublicArmor  string
	Fingerprint  string
}

type keyInfo struct {
	Fingerprint string
	UserIDs     []string
}

func canonicalFingerprint(value string) string {
	return strings.ToUpper(strings.ReplaceAll(strings.TrimSpace(value), " ", ""))
}

func generateF2FKey(name, email string, passphrase []byte) (*generatedKey, error) {
	name = strings.TrimSpace(name)
	email = strings.TrimSpace(email)
	if name == "" || len(name) > 128 || strings.ContainsAny(name, "\r\n") {
		return nil, errors.New("a short, single-line name is required")
	}
	if email == "" || len(email) > 254 || strings.ContainsAny(email, "\r\n<>") {
		return nil, errors.New("a valid email is required")
	}
	if len(passphrase) < minPassphraseBytes {
		return nil, fmt.Errorf("passphrase must be at least %d bytes", minPassphraseBytes)
	}

	pgp := crypto.PGP()
	key, err := pgp.KeyGeneration().
		AddUserId(name, email).
		New().
		GenerateKey()
	if err != nil {
		return nil, fmt.Errorf("generate key: %w", err)
	}
	locked, err := pgp.LockKey(key, passphrase)
	if err != nil {
		return nil, fmt.Errorf("lock private key: %w", err)
	}
	defer locked.ClearPrivateParams()

	privateArmor, err := locked.Armor()
	if err != nil {
		return nil, fmt.Errorf("armor private key: %w", err)
	}
	publicArmor, err := locked.GetArmoredPublicKey()
	if err != nil {
		return nil, fmt.Errorf("armor public key: %w", err)
	}
	return &generatedKey{
		PrivateArmor: privateArmor,
		PublicArmor:  publicArmor,
		Fingerprint:  canonicalFingerprint(locked.GetFingerprint()),
	}, nil
}

func unlockPrivateKey(armor string, passphrase []byte) (*crypto.Key, error) {
	key, err := crypto.NewPrivateKeyFromArmored(strings.TrimSpace(armor), passphrase)
	if err != nil {
		return nil, fmt.Errorf("unlock private key (wrong passphrase or unsupported key): %w", err)
	}
	unlocked, err := key.IsUnlocked()
	if err != nil || !unlocked {
		key.ClearPrivateParams()
		return nil, errors.New("private key is still locked")
	}
	now := time.Now().Unix()
	if key.IsExpired(now) {
		key.ClearPrivateParams()
		return nil, errors.New("private key is expired")
	}
	if key.IsRevoked(now) {
		key.ClearPrivateParams()
		return nil, errors.New("private key is revoked")
	}
	return key, nil
}

const (
	publicArmorBegin = "-----BEGIN PGP PUBLIC KEY BLOCK-----"
	publicArmorEnd   = "-----END PGP PUBLIC KEY BLOCK-----"
)

// splitArmorBlocks separates concatenated armored key blocks. armor.Decode
// wraps its input in a buffered reader and can consume the start of the next
// block, so feeding the whole keyring to it would silently drop every key
// after the first.
func splitArmorBlocks(input string) ([]string, error) {
	var blocks []string
	position := 0
	for {
		start := strings.Index(input[position:], publicArmorBegin)
		if start < 0 {
			if strings.TrimSpace(input[position:]) != "" {
				return nil, errors.New("unexpected text outside armored public-key blocks")
			}
			break
		}
		start += position
		if strings.TrimSpace(input[position:start]) != "" {
			return nil, errors.New("unexpected text outside armored public-key blocks")
		}
		endOffset := strings.Index(input[start:], publicArmorEnd)
		if endOffset < 0 {
			return nil, errors.New("unterminated armored public-key block")
		}
		end := start + endOffset + len(publicArmorEnd)
		blocks = append(blocks, input[start:end])
		position = end
	}
	if len(blocks) == 0 {
		return nil, errors.New("no armored public keys found")
	}
	return blocks, nil
}

// parsePublicKeyRing reads one or more armored public keys, rejects malformed
// input, and removes duplicate fingerprints.
func parsePublicKeyRing(armored string) (*crypto.KeyRing, []string, error) {
	if len(armored) > maxKeyringBytes {
		return nil, nil, fmt.Errorf("keyring is larger than %d bytes", maxKeyringBytes)
	}
	blocks, err := splitArmorBlocks(armored)
	if err != nil {
		return nil, nil, err
	}
	var entities openpgp.EntityList
	for _, block := range blocks {
		parsed, err := openpgp.ReadArmoredKeyRing(strings.NewReader(block))
		if err != nil {
			return nil, nil, fmt.Errorf("parse armored public key: %w", err)
		}
		entities = append(entities, parsed...)
	}
	if len(entities) == 0 {
		return nil, nil, errors.New("no public keys found")
	}
	if len(entities) > maxRecipientKeys {
		return nil, nil, fmt.Errorf("keyring has more than %d keys", maxRecipientKeys)
	}

	var ring *crypto.KeyRing
	fingerprints := make([]string, 0, len(entities))
	seen := make(map[string]bool)
	for _, entity := range entities {
		key, err := crypto.NewKeyFromEntity(entity)
		if err != nil {
			return nil, nil, fmt.Errorf("read public key: %w", err)
		}
		fingerprint := canonicalFingerprint(key.GetFingerprint())
		if seen[fingerprint] {
			continue
		}
		seen[fingerprint] = true
		if ring == nil {
			ring, err = crypto.NewKeyRing(key)
		} else {
			err = ring.AddKey(key)
		}
		if err != nil {
			return nil, nil, fmt.Errorf("build keyring: %w", err)
		}
		fingerprints = append(fingerprints, fingerprint)
	}
	if ring == nil || len(fingerprints) == 0 {
		return nil, nil, errors.New("no usable public keys found")
	}
	return ring, fingerprints, nil
}

func checkEncryptionKeys(ring *crypto.KeyRing) error {
	now := time.Now().Unix()
	for _, key := range ring.GetKeys() {
		if key.IsExpired(now) {
			return fmt.Errorf("recipient key %s is expired", canonicalFingerprint(key.GetFingerprint()))
		}
		if key.IsRevoked(now) {
			return fmt.Errorf("recipient key %s is revoked", canonicalFingerprint(key.GetFingerprint()))
		}
		if !key.CanEncrypt(now) {
			return fmt.Errorf("key %s cannot encrypt", canonicalFingerprint(key.GetFingerprint()))
		}
	}
	return nil
}

func encryptF2F(
	recipientArmor []string,
	signerArmor string,
	signerPassphrase, plaintext []byte,
	armored bool,
) ([]byte, []string, string, error) {
	if len(plaintext) == 0 {
		return nil, nil, "", errors.New("refusing to encrypt an empty message")
	}
	combined := strings.Join(recipientArmor, "\n")
	ring, fingerprints, err := parsePublicKeyRing(combined)
	if err != nil {
		return nil, nil, "", err
	}
	if err := checkEncryptionKeys(ring); err != nil {
		return nil, nil, "", err
	}

	pgp := crypto.PGP()
	builder := pgp.Encryption().
		Recipients(ring).
		CompressWith(constants.NoCompression)

	signerFingerprint := ""
	var signerKey *crypto.Key
	if signerArmor != "" {
		signerKey, err = unlockPrivateKey(signerArmor, signerPassphrase)
		if err != nil {
			return nil, nil, "", err
		}
		defer signerKey.ClearPrivateParams()
		entity := signerKey.GetEntity()
		if entity == nil {
			return nil, nil, "", errors.New("signer key has no OpenPGP entity")
		}
		if _, ok := entity.SigningKey(time.Now(), nil); !ok {
			return nil, nil, "", errors.New("signer key cannot sign")
		}
		builder = builder.SigningKey(signerKey)
		signerFingerprint = canonicalFingerprint(signerKey.GetFingerprint())
	}

	handle, err := builder.New()
	if err != nil {
		return nil, nil, "", fmt.Errorf("create encryption handle: %w", err)
	}
	defer handle.ClearPrivateParams()
	message, err := handle.Encrypt(plaintext)
	if err != nil {
		return nil, nil, "", fmt.Errorf("encrypt: %w", err)
	}
	var output []byte
	if armored {
		output, err = message.ArmorBytes()
	} else {
		output = message.Bytes()
	}
	if err != nil {
		return nil, nil, "", fmt.Errorf("armor message: %w", err)
	}
	return output, fingerprints, signerFingerprint, nil
}

func decryptF2F(
	privateArmor string,
	passphrase, ciphertext []byte,
	verifyArmor string,
	maxBytes int64,
) ([]byte, string, error) {
	key, err := unlockPrivateKey(privateArmor, passphrase)
	if err != nil {
		return nil, "", err
	}
	defer key.ClearPrivateParams()

	pgp := crypto.PGP()
	builder := pgp.Decryption().
		DecryptionKey(key).
		MaxDecompressedMessageSize(maxBytes)

	var verifyKey *crypto.Key
	verifyFingerprint := ""
	if verifyArmor != "" {
		ring, fingerprints, err := parsePublicKeyRing(verifyArmor)
		if err != nil {
			return nil, "", err
		}
		if len(fingerprints) != 1 {
			return nil, "", errors.New("verification key file must contain exactly one public key")
		}
		verifyKey, err = ring.GetKey(0)
		if err != nil {
			return nil, "", err
		}
		now := time.Now().Unix()
		if verifyKey.IsExpired(now) || verifyKey.IsRevoked(now) || !verifyKey.CanVerify(now) {
			return nil, "", errors.New("verification key cannot verify signatures")
		}
		verifyFingerprint = fingerprints[0]
		builder = builder.VerificationKey(verifyKey)
	}

	handle, err := builder.New()
	if err != nil {
		return nil, "", fmt.Errorf("create decryption handle: %w", err)
	}
	defer handle.ClearPrivateParams()
	result, err := handle.Decrypt(ciphertext, crypto.Auto)
	if err != nil {
		return nil, "", fmt.Errorf("decrypt (wrong key, corrupt, or plaintext message): %w", err)
	}
	plaintext := result.Bytes()
	if len(plaintext) == 0 {
		return nil, "", errors.New("decrypted message is empty")
	}
	if int64(len(plaintext)) > maxBytes {
		return nil, "", fmt.Errorf("decrypted message exceeds %d bytes", maxBytes)
	}

	if verifyArmor != "" {
		if len(result.Signatures) == 0 {
			return nil, "", errors.New("message is not signed, but a verification key was supplied")
		}
		if err := result.SignatureError(); err != nil {
			return nil, "", fmt.Errorf("signature verification failed: %w", err)
		}
		if !keyMatchesEntity(verifyKey.GetEntity(), result.SignedByFingerprint()) {
			return nil, "", fmt.Errorf(
				"message is signed by %s, not the supplied key %s",
				canonicalFingerprint(hex.EncodeToString(result.SignedByFingerprint())),
				verifyFingerprint,
			)
		}
		return plaintext, verifyFingerprint, nil
	}
	return plaintext, "", nil
}

func keyMatchesEntity(entity *openpgp.Entity, fingerprint []byte) bool {
	if entity == nil || entity.PrimaryKey == nil || len(fingerprint) == 0 {
		return false
	}
	if bytes.Equal(entity.PrimaryKey.Fingerprint, fingerprint) {
		return true
	}
	for _, subkey := range entity.Subkeys {
		if subkey.PublicKey != nil && bytes.Equal(subkey.PublicKey.Fingerprint, fingerprint) {
			return true
		}
	}
	return false
}

func publicKeyFingerprint(armor string) (string, error) {
	ring, fingerprints, err := parsePublicKeyRing(armor)
	if err != nil {
		return "", err
	}
	if ring.CountEntities() != 1 || len(fingerprints) != 1 {
		return "", errors.New("exactly one public key is required")
	}
	return fingerprints[0], nil
}

func mergeKeyrings(armors []string) (string, []string, error) {
	var blocks []string
	var fingerprints []string
	seen := make(map[string]bool)
	for _, armor := range armors {
		ring, keys, err := parsePublicKeyRing(armor)
		if err != nil {
			return "", nil, err
		}
		for index, fingerprint := range keys {
			if seen[fingerprint] {
				continue
			}
			key, err := ring.GetKey(index)
			if err != nil {
				return "", nil, err
			}
			publicArmor, err := key.GetArmoredPublicKey()
			if err != nil {
				return "", nil, err
			}
			seen[fingerprint] = true
			blocks = append(blocks, strings.TrimSpace(publicArmor))
			fingerprints = append(fingerprints, fingerprint)
		}
	}
	if len(blocks) == 0 {
		return "", nil, errors.New("no public keys found")
	}
	return strings.Join(blocks, "\n\n") + "\n", fingerprints, nil
}

func listKeyring(armor string) ([]keyInfo, error) {
	ring, fingerprints, err := parsePublicKeyRing(armor)
	if err != nil {
		return nil, err
	}
	infos := make([]keyInfo, 0, len(fingerprints))
	for index, fingerprint := range fingerprints {
		key, err := ring.GetKey(index)
		if err != nil {
			return nil, err
		}
		entity := key.GetEntity()
		var userIDs []string
		if entity != nil {
			for _, identity := range entity.Identities {
				if identity.UserId != nil {
					userIDs = append(userIDs, identity.UserId.Name+" <"+identity.UserId.Email+">")
				}
			}
		}
		sort.Strings(userIDs)
		infos = append(infos, keyInfo{Fingerprint: fingerprint, UserIDs: userIDs})
	}
	return infos, nil
}
