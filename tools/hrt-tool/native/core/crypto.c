#include "hrtcore/hrtcore.h"

#include <limits.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <string.h>

#define HRTCORE_GCM_TAG_LEN 16
#define HRTCORE_ETM_MAC_LEN 32
#define HRTCORE_AES_KEY_LEN 32
#define HRTCORE_GCM_NONCE_LEN 12
#define HRTCORE_CBC_IV_LEN 16

static void set_error(hrtcore_error *err, int code, const char *message) {
    if (err == NULL) {
        return;
    }
    err->code = code;
    if (message == NULL) {
        err->message[0] = '\0';
        return;
    }
    snprintf(err->message, sizeof(err->message), "%s", message);
}

void hrtcore_error_clear(hrtcore_error *err) {
    set_error(err, HRTCORE_OK, NULL);
}

const char *hrtcore_error_message(const hrtcore_error *err) {
    return err == NULL ? "" : err->message;
}

void hrtcore_secure_zero(void *buf, size_t len) {
    if (buf != NULL && len != 0) {
        OPENSSL_cleanse(buf, len);
    }
}

int hrtcore_random_bytes(uint8_t *out, size_t len, hrtcore_error *err) {
    hrtcore_error_clear(err);
    if ((out == NULL && len != 0) || len > (size_t)INT_MAX) {
        set_error(err, HRTCORE_ERR_INVALID_ARG, "invalid random byte request");
        return HRTCORE_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return HRTCORE_OK;
    }
    if (RAND_bytes(out, (int)len) != 1) {
        set_error(err, HRTCORE_ERR_CRYPTO, "random byte generation failed");
        return HRTCORE_ERR_CRYPTO;
    }
    return HRTCORE_OK;
}

int hrtcore_pbkdf2_sha256(const uint8_t *password, size_t password_len,
                          const uint8_t *salt, size_t salt_len, uint32_t iterations,
                          uint8_t *out, size_t out_len, hrtcore_error *err) {
    hrtcore_error_clear(err);
    if ((password == NULL && password_len != 0) || (salt == NULL && salt_len != 0) ||
        out == NULL || out_len == 0 || iterations == 0) {
        set_error(err, HRTCORE_ERR_INVALID_ARG, "invalid PBKDF2 argument");
        return HRTCORE_ERR_INVALID_ARG;
    }
    if (password_len > (size_t)INT_MAX || salt_len > (size_t)INT_MAX ||
        out_len > (size_t)INT_MAX || iterations > (uint32_t)INT_MAX) {
        set_error(err, HRTCORE_ERR_LIMIT, "PBKDF2 parameter too large");
        return HRTCORE_ERR_LIMIT;
    }
    if (PKCS5_PBKDF2_HMAC((const char *)password, (int)password_len, salt, (int)salt_len,
                          (int)iterations, EVP_sha256(), (int)out_len, out) != 1) {
        set_error(err, HRTCORE_ERR_CRYPTO, "PBKDF2 derivation failed");
        return HRTCORE_ERR_CRYPTO;
    }
    return HRTCORE_OK;
}

int hrtcore_hkdf_sha256(const uint8_t *ikm, size_t ikm_len, const uint8_t *salt,
                        size_t salt_len, const uint8_t *info, size_t info_len,
                        uint8_t *out, size_t out_len, hrtcore_error *err) {
    hrtcore_error_clear(err);
    if ((ikm == NULL && ikm_len != 0) || (salt == NULL && salt_len != 0) ||
        (info == NULL && info_len != 0) || out == NULL || out_len == 0) {
        set_error(err, HRTCORE_ERR_INVALID_ARG, "invalid HKDF argument");
        return HRTCORE_ERR_INVALID_ARG;
    }
    if (ikm_len > (size_t)INT_MAX || salt_len > (size_t)INT_MAX ||
        info_len > (size_t)INT_MAX || out_len > (size_t)INT_MAX) {
        set_error(err, HRTCORE_ERR_LIMIT, "HKDF parameter too large");
        return HRTCORE_ERR_LIMIT;
    }

    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL);
    if (ctx == NULL) {
        set_error(err, HRTCORE_ERR_CRYPTO, "HKDF context allocation failed");
        return HRTCORE_ERR_CRYPTO;
    }
    int ok = EVP_PKEY_derive_init(ctx) == 1 &&
             EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) == 1 &&
             EVP_PKEY_CTX_set1_hkdf_salt(ctx, salt, (int)salt_len) == 1 &&
             EVP_PKEY_CTX_set1_hkdf_key(ctx, ikm, (int)ikm_len) == 1;
    if (ok && info_len != 0) {
        ok = EVP_PKEY_CTX_add1_hkdf_info(ctx, info, (int)info_len) == 1;
    }
    size_t derived_len = out_len;
    if (ok) {
        ok = EVP_PKEY_derive(ctx, out, &derived_len) == 1 && derived_len == out_len;
    }
    EVP_PKEY_CTX_free(ctx);
    if (!ok) {
        set_error(err, HRTCORE_ERR_CRYPTO, "HKDF derivation failed");
        return HRTCORE_ERR_CRYPTO;
    }
    return HRTCORE_OK;
}

int hrtcore_aes256gcm_encrypt(const uint8_t *key, size_t key_len, const uint8_t *nonce,
                              size_t nonce_len, const uint8_t *aad, size_t aad_len,
                              const uint8_t *plaintext, size_t plaintext_len, uint8_t *out,
                              size_t out_cap, size_t *out_len, hrtcore_error *err) {
    hrtcore_error_clear(err);
    if (key == NULL || key_len != HRTCORE_AES_KEY_LEN || nonce == NULL ||
        nonce_len != HRTCORE_GCM_NONCE_LEN || out == NULL || out_len == NULL ||
        (plaintext == NULL && plaintext_len != 0) || (aad == NULL && aad_len != 0)) {
        set_error(err, HRTCORE_ERR_INVALID_ARG, "invalid AES-GCM encrypt argument");
        return HRTCORE_ERR_INVALID_ARG;
    }
    if (plaintext_len > SIZE_MAX - HRTCORE_GCM_TAG_LEN ||
        out_cap < plaintext_len + HRTCORE_GCM_TAG_LEN) {
        set_error(err, HRTCORE_ERR_BUFFER_TOO_SMALL, "AES-GCM output buffer too small");
        return HRTCORE_ERR_BUFFER_TOO_SMALL;
    }
    if (plaintext_len > (size_t)INT_MAX || aad_len > (size_t)INT_MAX) {
        set_error(err, HRTCORE_ERR_LIMIT, "AES-GCM input too large");
        return HRTCORE_ERR_LIMIT;
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        set_error(err, HRTCORE_ERR_CRYPTO, "AES-GCM context allocation failed");
        return HRTCORE_ERR_CRYPTO;
    }

    int len = 0;
    int total = 0;
    int ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
             EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)nonce_len, NULL) == 1 &&
             EVP_EncryptInit_ex(ctx, NULL, NULL, key, nonce) == 1;
    if (ok && aad_len != 0) {
        ok = EVP_EncryptUpdate(ctx, NULL, &len, aad, (int)aad_len) == 1;
    }
    if (ok && plaintext_len != 0) {
        ok = EVP_EncryptUpdate(ctx, out, &len, plaintext, (int)plaintext_len) == 1;
        total = len;
    }
    if (ok) {
        ok = EVP_EncryptFinal_ex(ctx, out + total, &len) == 1;
        total += len;
    }
    if (ok) {
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, HRTCORE_GCM_TAG_LEN,
                                 out + total) == 1;
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        set_error(err, HRTCORE_ERR_CRYPTO, "AES-GCM encryption failed");
        return HRTCORE_ERR_CRYPTO;
    }
    *out_len = (size_t)total + HRTCORE_GCM_TAG_LEN;
    return HRTCORE_OK;
}

int hrtcore_aes256gcm_decrypt(const uint8_t *key, size_t key_len, const uint8_t *nonce,
                              size_t nonce_len, const uint8_t *aad, size_t aad_len,
                              const uint8_t *ciphertext, size_t ciphertext_len, uint8_t *out,
                              size_t out_cap, size_t *out_len, hrtcore_error *err) {
    hrtcore_error_clear(err);
    if (key == NULL || key_len != HRTCORE_AES_KEY_LEN || nonce == NULL ||
        nonce_len != HRTCORE_GCM_NONCE_LEN || ciphertext == NULL || out_len == NULL ||
        (out == NULL && out_cap != 0) || (aad == NULL && aad_len != 0)) {
        set_error(err, HRTCORE_ERR_INVALID_ARG, "invalid AES-GCM decrypt argument");
        return HRTCORE_ERR_INVALID_ARG;
    }
    if (ciphertext_len < HRTCORE_GCM_TAG_LEN) {
        set_error(err, HRTCORE_ERR_AUTH, "AES-GCM ciphertext shorter than the tag");
        return HRTCORE_ERR_AUTH;
    }
    const size_t ct_len = ciphertext_len - HRTCORE_GCM_TAG_LEN;
    if (out_cap < ct_len) {
        set_error(err, HRTCORE_ERR_BUFFER_TOO_SMALL, "AES-GCM output buffer too small");
        return HRTCORE_ERR_BUFFER_TOO_SMALL;
    }
    if (ct_len > (size_t)INT_MAX || aad_len > (size_t)INT_MAX) {
        set_error(err, HRTCORE_ERR_LIMIT, "AES-GCM input too large");
        return HRTCORE_ERR_LIMIT;
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        set_error(err, HRTCORE_ERR_CRYPTO, "AES-GCM context allocation failed");
        return HRTCORE_ERR_CRYPTO;
    }

    int len = 0;
    int total = 0;
    int ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
             EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)nonce_len, NULL) == 1 &&
             EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) == 1;
    if (ok && aad_len != 0) {
        ok = EVP_DecryptUpdate(ctx, NULL, &len, aad, (int)aad_len) == 1;
    }
    if (ok && ct_len != 0) {
        ok = EVP_DecryptUpdate(ctx, out, &len, ciphertext, (int)ct_len) == 1;
        total = len;
    }
    if (ok) {
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, HRTCORE_GCM_TAG_LEN,
                                 (void *)(ciphertext + ct_len)) == 1;
    }
    if (ok) {
        ok = EVP_DecryptFinal_ex(ctx, out + total, &len) == 1;
        total += len;
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        set_error(err, HRTCORE_ERR_AUTH, "AES-GCM authentication failed");
        return HRTCORE_ERR_AUTH;
    }
    *out_len = (size_t)total;
    return HRTCORE_OK;
}

static int hmac_sha256_parts(const uint8_t *key, size_t key_len, const uint8_t *aad,
                             size_t aad_len, const uint8_t *iv, size_t iv_len,
                             const uint8_t *ct, size_t ct_len, uint8_t out[32]) {
    if (key_len > (size_t)INT_MAX || aad_len > (size_t)INT_MAX ||
        iv_len > (size_t)INT_MAX || ct_len > (size_t)INT_MAX) {
        return 0;
    }

    EVP_MAC *mac = EVP_MAC_fetch(NULL, "HMAC", NULL);
    if (mac == NULL) {
        return 0;
    }
    EVP_MAC_CTX *ctx = EVP_MAC_CTX_new(mac);
    OSSL_PARAM params[2];
    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, (char *)"SHA256", 0);
    params[1] = OSSL_PARAM_construct_end();

    int ok = ctx != NULL && EVP_MAC_init(ctx, key, key_len, params) == 1;
    if (ok && aad_len != 0) {
        ok = EVP_MAC_update(ctx, aad, aad_len) == 1;
    }
    if (ok && iv_len != 0) {
        ok = EVP_MAC_update(ctx, iv, iv_len) == 1;
    }
    if (ok && ct_len != 0) {
        ok = EVP_MAC_update(ctx, ct, ct_len) == 1;
    }
    size_t mac_len = 0;
    if (ok) {
        ok = EVP_MAC_final(ctx, out, &mac_len, 32) == 1 && mac_len == 32;
    }

    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    return ok;
}

int hrtcore_etm_encrypt(const uint8_t *enc_key, const uint8_t *mac_key, const uint8_t *iv,
                        size_t iv_len, const uint8_t *aad, size_t aad_len,
                        const uint8_t *plaintext, size_t plaintext_len, uint8_t *out,
                        size_t out_cap, size_t *out_len, hrtcore_error *err) {
    hrtcore_error_clear(err);
    if (enc_key == NULL || mac_key == NULL || iv == NULL || iv_len != HRTCORE_CBC_IV_LEN ||
        out == NULL || out_len == NULL || (plaintext == NULL && plaintext_len != 0) ||
        (aad == NULL && aad_len != 0)) {
        set_error(err, HRTCORE_ERR_INVALID_ARG, "invalid EtM encrypt argument");
        return HRTCORE_ERR_INVALID_ARG;
    }
    if (plaintext_len > SIZE_MAX - HRTCORE_CBC_IV_LEN - HRTCORE_ETM_MAC_LEN ||
        out_cap < plaintext_len + HRTCORE_CBC_IV_LEN + HRTCORE_ETM_MAC_LEN) {
        set_error(err, HRTCORE_ERR_BUFFER_TOO_SMALL, "EtM output buffer too small");
        return HRTCORE_ERR_BUFFER_TOO_SMALL;
    }
    if (plaintext_len > (size_t)INT_MAX || aad_len > (size_t)INT_MAX) {
        set_error(err, HRTCORE_ERR_LIMIT, "EtM input too large");
        return HRTCORE_ERR_LIMIT;
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        set_error(err, HRTCORE_ERR_CRYPTO, "EtM context allocation failed");
        return HRTCORE_ERR_CRYPTO;
    }

    int len = 0;
    int total = 0;
    int ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, enc_key, iv) == 1;
    if (ok && plaintext_len != 0) {
        ok = EVP_EncryptUpdate(ctx, out, &len, plaintext, (int)plaintext_len) == 1;
        total = len;
    }
    if (ok) {
        ok = EVP_EncryptFinal_ex(ctx, out + total, &len) == 1;
        total += len;
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        set_error(err, HRTCORE_ERR_CRYPTO, "EtM encryption failed");
        return HRTCORE_ERR_CRYPTO;
    }

    if (!hmac_sha256_parts(mac_key, HRTCORE_AES_KEY_LEN, aad, aad_len, iv, iv_len, out,
                           (size_t)total, out + total)) {
        set_error(err, HRTCORE_ERR_CRYPTO, "EtM MAC computation failed");
        return HRTCORE_ERR_CRYPTO;
    }
    *out_len = (size_t)total + HRTCORE_ETM_MAC_LEN;
    return HRTCORE_OK;
}

int hrtcore_etm_decrypt(const uint8_t *enc_key, const uint8_t *mac_key, const uint8_t *iv,
                        size_t iv_len, const uint8_t *aad, size_t aad_len,
                        const uint8_t *ciphertext, size_t ciphertext_len, uint8_t *out,
                        size_t out_cap, size_t *out_len, hrtcore_error *err) {
    hrtcore_error_clear(err);
    if (enc_key == NULL || mac_key == NULL || iv == NULL || iv_len != HRTCORE_CBC_IV_LEN ||
        ciphertext == NULL || out_len == NULL || (out == NULL && out_cap != 0) ||
        (aad == NULL && aad_len != 0)) {
        set_error(err, HRTCORE_ERR_INVALID_ARG, "invalid EtM decrypt argument");
        return HRTCORE_ERR_INVALID_ARG;
    }
    if (ciphertext_len < HRTCORE_ETM_MAC_LEN) {
        set_error(err, HRTCORE_ERR_AUTH, "EtM ciphertext shorter than the MAC");
        return HRTCORE_ERR_AUTH;
    }
    const size_t ct_len = ciphertext_len - HRTCORE_ETM_MAC_LEN;
    if (ct_len % HRTCORE_CBC_IV_LEN != 0) {
        set_error(err, HRTCORE_ERR_AUTH, "EtM ciphertext is not a whole number of blocks");
        return HRTCORE_ERR_AUTH;
    }
    if (out_cap < ct_len) {
        set_error(err, HRTCORE_ERR_BUFFER_TOO_SMALL, "EtM output buffer too small");
        return HRTCORE_ERR_BUFFER_TOO_SMALL;
    }
    if (ct_len > (size_t)INT_MAX || aad_len > (size_t)INT_MAX) {
        set_error(err, HRTCORE_ERR_LIMIT, "EtM input too large");
        return HRTCORE_ERR_LIMIT;
    }

    uint8_t expected_mac[32];
    if (!hmac_sha256_parts(mac_key, HRTCORE_AES_KEY_LEN, aad, aad_len, iv, iv_len,
                           ciphertext, ct_len, expected_mac)) {
        set_error(err, HRTCORE_ERR_CRYPTO, "EtM MAC computation failed");
        return HRTCORE_ERR_CRYPTO;
    }
    if (CRYPTO_memcmp(expected_mac, ciphertext + ct_len, HRTCORE_ETM_MAC_LEN) != 0) {
        hrtcore_secure_zero(expected_mac, sizeof(expected_mac));
        set_error(err, HRTCORE_ERR_AUTH, "EtM authentication failed");
        return HRTCORE_ERR_AUTH;
    }
    hrtcore_secure_zero(expected_mac, sizeof(expected_mac));

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        set_error(err, HRTCORE_ERR_CRYPTO, "EtM context allocation failed");
        return HRTCORE_ERR_CRYPTO;
    }

    int len = 0;
    int total = 0;
    int ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, enc_key, iv) == 1;
    if (ok && ct_len != 0) {
        ok = EVP_DecryptUpdate(ctx, out, &len, ciphertext, (int)ct_len) == 1;
        total = len;
    }
    if (ok) {
        ok = EVP_DecryptFinal_ex(ctx, out + total, &len) == 1;
        total += len;
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        set_error(err, HRTCORE_ERR_AUTH, "EtM decryption failed");
        return HRTCORE_ERR_AUTH;
    }
    *out_len = (size_t)total;
    return HRTCORE_OK;
}
