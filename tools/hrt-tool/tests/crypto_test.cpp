#include "hrtcore/hrtcore.h"
#include "test_framework.h"

#include <cstring>
#include <vector>

namespace {

std::vector<uint8_t> bytes(size_t len, uint8_t value) {
    return std::vector<uint8_t>(len, value);
}

void test_base64() {
    hrtcore_error err{};
    char encoded[32];
    size_t encoded_len = 0;

    CHECK_EQ(hrtcore_b64_encode(reinterpret_cast<const uint8_t *>("f"), 1, encoded,
                                sizeof(encoded), &encoded_len, &err),
             HRTCORE_OK);
    CHECK_EQ(std::string(encoded, encoded_len), std::string("Zg=="));

    CHECK_EQ(hrtcore_b64_encode(reinterpret_cast<const uint8_t *>("foobar"), 6, encoded,
                                sizeof(encoded), &encoded_len, &err),
             HRTCORE_OK);
    CHECK_EQ(std::string(encoded, encoded_len), std::string("Zm9vYmFy"));

    uint8_t decoded[16];
    size_t decoded_len = 0;
    CHECK_EQ(hrtcore_b64_decode("Zg==", 4, decoded, sizeof(decoded), &decoded_len, &err),
             HRTCORE_OK);
    CHECK_EQ(decoded_len, static_cast<size_t>(1));
    CHECK_EQ(decoded[0], static_cast<uint8_t>('f'));

    char tiny[1];
    CHECK_EQ(hrtcore_b64_encode(reinterpret_cast<const uint8_t *>("f"), 1, tiny,
                                sizeof(tiny), &encoded_len, &err),
             HRTCORE_ERR_BUFFER_TOO_SMALL);
}

void test_pbkdf2() {
    hrtcore_error err{};
    uint8_t out[32];
    const char *password = "password";
    const char *salt = "salt";

    CHECK_EQ(hrtcore_pbkdf2_sha256(reinterpret_cast<const uint8_t *>(password),
                                   std::strlen(password),
                                   reinterpret_cast<const uint8_t *>(salt), std::strlen(salt),
                                   1, out, sizeof(out), &err),
             HRTCORE_OK);
    CHECK_HEX(out, sizeof(out),
              "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");

    CHECK_EQ(hrtcore_pbkdf2_sha256(reinterpret_cast<const uint8_t *>(password),
                                   std::strlen(password),
                                   reinterpret_cast<const uint8_t *>(salt), std::strlen(salt),
                                   2, out, sizeof(out), &err),
             HRTCORE_OK);
    CHECK_HEX(out, sizeof(out),
              "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
}

void test_hkdf() {
    hrtcore_error err{};
    const uint8_t ikm[22] = {0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
                             0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b};
    uint8_t salt[13];
    size_t salt_len = 0;
    CHECK(hex_decode("000102030405060708090a0b0c", salt, sizeof(salt), &salt_len));
    uint8_t info[10];
    size_t info_len = 0;
    CHECK(hex_decode("f0f1f2f3f4f5f6f7f8f9", info, sizeof(info), &info_len));

    uint8_t okm[42];
    CHECK_EQ(hrtcore_hkdf_sha256(ikm, sizeof(ikm), salt, salt_len, info, info_len, okm,
                                 sizeof(okm), &err),
             HRTCORE_OK);
    CHECK_HEX(okm, sizeof(okm),
              "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b8"
              "87185865");
}

void test_aes_gcm() {
    hrtcore_error err{};
    uint8_t key[32] = {};
    uint8_t nonce[12] = {};
    uint8_t tag[16];
    size_t tag_len = 0;

    CHECK_EQ(hrtcore_aes256gcm_encrypt(key, sizeof(key), nonce, sizeof(nonce), nullptr, 0,
                                       nullptr, 0, tag, sizeof(tag), &tag_len, &err),
             HRTCORE_OK);
    CHECK_EQ(tag_len, sizeof(tag));
    CHECK_HEX(tag, sizeof(tag), "530f8afbc74536b9a963b4f1c4cb738b");

    for (size_t i = 0; i < sizeof(key); ++i) {
        key[i] = static_cast<uint8_t>(i);
    }
    const char *plaintext = "hello";
    const char *aad = "aad";
    uint8_t sealed[64];
    size_t sealed_len = 0;
    CHECK_EQ(hrtcore_aes256gcm_encrypt(key, sizeof(key), nonce, sizeof(nonce),
                                       reinterpret_cast<const uint8_t *>(aad), std::strlen(aad),
                                       reinterpret_cast<const uint8_t *>(plaintext),
                                       std::strlen(plaintext), sealed, sizeof(sealed),
                                       &sealed_len, &err),
             HRTCORE_OK);
    CHECK_EQ(sealed_len, std::strlen(plaintext) + 16);

    uint8_t opened[64];
    size_t opened_len = 0;
    CHECK_EQ(hrtcore_aes256gcm_decrypt(key, sizeof(key), nonce, sizeof(nonce),
                                       reinterpret_cast<const uint8_t *>(aad), std::strlen(aad),
                                       sealed, sealed_len, opened, sizeof(opened), &opened_len,
                                       &err),
             HRTCORE_OK);
    CHECK_EQ(std::string(reinterpret_cast<char *>(opened), opened_len), std::string(plaintext));

    sealed[0] ^= 0x01;
    CHECK_EQ(hrtcore_aes256gcm_decrypt(key, sizeof(key), nonce, sizeof(nonce),
                                       reinterpret_cast<const uint8_t *>(aad), std::strlen(aad),
                                       sealed, sealed_len, opened, sizeof(opened), &opened_len,
                                       &err),
             HRTCORE_ERR_AUTH);
    CHECK(std::strlen(hrtcore_error_message(&err)) > 0);

    uint8_t tiny[1];
    size_t tiny_len = 0;
    CHECK_EQ(hrtcore_aes256gcm_encrypt(key, sizeof(key), nonce, sizeof(nonce), nullptr, 0,
                                       reinterpret_cast<const uint8_t *>(plaintext),
                                       std::strlen(plaintext), tiny, sizeof(tiny), &tiny_len,
                                       &err),
             HRTCORE_ERR_BUFFER_TOO_SMALL);
}

void test_etm() {
    hrtcore_error err{};
    uint8_t enc_key[32];
    uint8_t mac_key[32];
    uint8_t iv[16];
    for (size_t i = 0; i < 32; ++i) {
        enc_key[i] = static_cast<uint8_t>(i);
        mac_key[i] = static_cast<uint8_t>(255 - i);
    }
    for (size_t i = 0; i < sizeof(iv); ++i) {
        iv[i] = static_cast<uint8_t>(i * 7);
    }
    const char *aad = "transport-v1";
    const char *plaintext = "hello world";

    uint8_t sealed[128];
    size_t sealed_len = 0;
    CHECK_EQ(hrtcore_etm_encrypt(enc_key, mac_key, iv, sizeof(iv),
                                 reinterpret_cast<const uint8_t *>(aad), std::strlen(aad),
                                 reinterpret_cast<const uint8_t *>(plaintext),
                                 std::strlen(plaintext), sealed, sizeof(sealed), &sealed_len,
                                 &err),
             HRTCORE_OK);

    uint8_t opened[128];
    size_t opened_len = 0;
    CHECK_EQ(hrtcore_etm_decrypt(enc_key, mac_key, iv, sizeof(iv),
                                 reinterpret_cast<const uint8_t *>(aad), std::strlen(aad), sealed,
                                 sealed_len, opened, sizeof(opened), &opened_len, &err),
             HRTCORE_OK);
    CHECK_EQ(std::string(reinterpret_cast<char *>(opened), opened_len), std::string(plaintext));

    sealed[0] ^= 0x01;
    CHECK_EQ(hrtcore_etm_decrypt(enc_key, mac_key, iv, sizeof(iv),
                                 reinterpret_cast<const uint8_t *>(aad), std::strlen(aad), sealed,
                                 sealed_len, opened, sizeof(opened), &opened_len, &err),
             HRTCORE_ERR_AUTH);

    sealed[0] ^= 0x01;
    sealed[sealed_len - 1] ^= 0x01;
    CHECK_EQ(hrtcore_etm_decrypt(enc_key, mac_key, iv, sizeof(iv),
                                 reinterpret_cast<const uint8_t *>(aad), std::strlen(aad), sealed,
                                 sealed_len, opened, sizeof(opened), &opened_len, &err),
             HRTCORE_ERR_AUTH);

    sealed[sealed_len - 1] ^= 0x01;
    const char *other_aad = "transport-v2";
    CHECK_EQ(hrtcore_etm_decrypt(enc_key, mac_key, iv, sizeof(iv),
                                 reinterpret_cast<const uint8_t *>(other_aad),
                                 std::strlen(other_aad), sealed, sealed_len, opened,
                                 sizeof(opened), &opened_len, &err),
             HRTCORE_ERR_AUTH);
}

void test_misc() {
    hrtcore_error err{};
    uint8_t secret[32];
    std::memset(secret, 0xaa, sizeof(secret));
    hrtcore_secure_zero(secret, sizeof(secret));
    for (uint8_t byte : secret) {
        CHECK_EQ(byte, static_cast<uint8_t>(0));
    }

    uint8_t random_a[32] = {};
    uint8_t random_b[32] = {};
    CHECK_EQ(hrtcore_random_bytes(random_a, sizeof(random_a), &err), HRTCORE_OK);
    CHECK_EQ(hrtcore_random_bytes(random_b, sizeof(random_b), &err), HRTCORE_OK);
    CHECK(std::memcmp(random_a, random_b, sizeof(random_a)) != 0);

    uint8_t key[16] = {};
    uint8_t nonce[12] = {};
    uint8_t out[32];
    size_t out_len = 0;
    CHECK_EQ(hrtcore_aes256gcm_encrypt(key, sizeof(key), nonce, sizeof(nonce), nullptr, 0,
                                       nullptr, 0, out, sizeof(out), &out_len, &err),
             HRTCORE_ERR_INVALID_ARG);
}

} // namespace

int main() {
    test_base64();
    test_pbkdf2();
    test_hkdf();
    test_aes_gcm();
    test_etm();
    test_misc();
    return test_result();
}
