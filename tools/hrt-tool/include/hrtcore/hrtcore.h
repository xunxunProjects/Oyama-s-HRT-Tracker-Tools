#ifndef HRTCORE_H
#define HRTCORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HRTCORE_ABI_VERSION 1u

#define HRTCORE_OK 0
#define HRTCORE_ERR_INVALID_ARG 1
#define HRTCORE_ERR_BUFFER_TOO_SMALL 2
#define HRTCORE_ERR_CRYPTO 3
#define HRTCORE_ERR_AUTH 4
#define HRTCORE_ERR_LIMIT 5

#define HRTCORE_ERROR_MESSAGE_CAP 192

typedef struct hrtcore_error {
    int code;
    char message[HRTCORE_ERROR_MESSAGE_CAP];
} hrtcore_error;

/* Version of the C core implementation. */
const char *hrtcore_version(void);

/* ABI revision of this header. */
uint32_t hrtcore_abi_version(void);

void hrtcore_error_clear(hrtcore_error *err);
const char *hrtcore_error_message(const hrtcore_error *err);

/* Best-effort zeroization that the compiler may not elide. */
void hrtcore_secure_zero(void *buf, size_t len);

int hrtcore_random_bytes(uint8_t *out, size_t len, hrtcore_error *err);

/* Standard base64 with padding. Output is NUL-terminated when out_cap > out_len. */
int hrtcore_b64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap,
                       size_t *out_len, hrtcore_error *err);
int hrtcore_b64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap,
                       size_t *out_len, hrtcore_error *err);

int hrtcore_pbkdf2_sha256(const uint8_t *password, size_t password_len,
                          const uint8_t *salt, size_t salt_len, uint32_t iterations,
                          uint8_t *out, size_t out_len, hrtcore_error *err);

int hrtcore_hkdf_sha256(const uint8_t *ikm, size_t ikm_len, const uint8_t *salt,
                        size_t salt_len, const uint8_t *info, size_t info_len,
                        uint8_t *out, size_t out_len, hrtcore_error *err);

/* AES-256-GCM. Output is ciphertext || 16-byte tag. nonce_len must be 12. */
int hrtcore_aes256gcm_encrypt(const uint8_t *key, size_t key_len, const uint8_t *nonce,
                              size_t nonce_len, const uint8_t *aad, size_t aad_len,
                              const uint8_t *plaintext, size_t plaintext_len, uint8_t *out,
                              size_t out_cap, size_t *out_len, hrtcore_error *err);
int hrtcore_aes256gcm_decrypt(const uint8_t *key, size_t key_len, const uint8_t *nonce,
                              size_t nonce_len, const uint8_t *aad, size_t aad_len,
                              const uint8_t *ciphertext, size_t ciphertext_len, uint8_t *out,
                              size_t out_cap, size_t *out_len, hrtcore_error *err);

/*
 * Encrypt-then-MAC: AES-256-CBC with PKCS#7, then HMAC-SHA256 over
 * aad || iv || ciphertext. Output is ciphertext || 32-byte MAC. iv_len must be 16.
 */
int hrtcore_etm_encrypt(const uint8_t *enc_key, const uint8_t *mac_key, const uint8_t *iv,
                        size_t iv_len, const uint8_t *aad, size_t aad_len,
                        const uint8_t *plaintext, size_t plaintext_len, uint8_t *out,
                        size_t out_cap, size_t *out_len, hrtcore_error *err);
int hrtcore_etm_decrypt(const uint8_t *enc_key, const uint8_t *mac_key, const uint8_t *iv,
                        size_t iv_len, const uint8_t *aad, size_t aad_len,
                        const uint8_t *ciphertext, size_t ciphertext_len, uint8_t *out,
                        size_t out_cap, size_t *out_len, hrtcore_error *err);

#ifdef __cplusplus
}
#endif

#endif /* HRTCORE_H */
