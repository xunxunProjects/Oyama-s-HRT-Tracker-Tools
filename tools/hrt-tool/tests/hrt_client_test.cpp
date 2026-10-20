#include "error.h"
#include "hrt_client.h"
#include "test_framework.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <memory>
#include <string>

namespace {

struct BioDeleter {
    void operator()(BIO *bio) const { BIO_free(bio); }
};
struct CertDeleter {
    void operator()(X509 *cert) const { X509_free(cert); }
};
struct KeyDeleter {
    void operator()(EVP_PKEY *key) const { EVP_PKEY_free(key); }
};

bool all_hex(const std::string &value) {
    for (char c : value) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    {
        const HrtDeviceIdentity identity = generate_hrt_device_identity("hrt-tool test");
        CHECK(!identity.private_key_pem.empty());
        CHECK(!identity.certificate_pem.empty());
        CHECK_EQ(identity.fingerprint.size(), static_cast<size_t>(64));
        CHECK(all_hex(identity.fingerprint));

        std::unique_ptr<BIO, BioDeleter> cert_bio(
            BIO_new_mem_buf(identity.certificate_pem.data(),
                            static_cast<int>(identity.certificate_pem.size())));
        std::unique_ptr<X509, CertDeleter> cert(
            cert_bio ? PEM_read_bio_X509(cert_bio.get(), nullptr, nullptr, nullptr)
                     : nullptr);
        std::unique_ptr<BIO, BioDeleter> key_bio(
            BIO_new_mem_buf(identity.private_key_pem.data(),
                            static_cast<int>(identity.private_key_pem.size())));
        std::unique_ptr<EVP_PKEY, KeyDeleter> key(
            key_bio ? PEM_read_bio_PrivateKey(key_bio.get(), nullptr, nullptr, nullptr)
                    : nullptr);
        CHECK(cert != nullptr);
        CHECK(key != nullptr);
        if (cert && key) {
            CHECK_EQ(X509_check_private_key(cert.get(), key.get()), 1);
            char common_name[64] = {};
            X509_NAME_get_text_by_NID(X509_get_subject_name(cert.get()), NID_commonName,
                                      common_name, sizeof(common_name));
            CHECK_EQ(std::string(common_name), std::string("hrt-tool test"));

            unsigned char *der = nullptr;
            const int der_length = i2d_X509(cert.get(), &der);
            CHECK(der_length > 0);
            unsigned char digest[EVP_MAX_MD_SIZE] = {};
            unsigned int digest_length = 0;
            CHECK_EQ(EVP_Digest(der, static_cast<size_t>(der_length), digest,
                                &digest_length, EVP_sha256(), nullptr),
                     1);
            OPENSSL_free(der);
            std::string hex;
            static const char digits[] = "0123456789abcdef";
            for (unsigned int i = 0; i < digest_length; ++i) {
                hex.push_back(digits[digest[i] >> 4]);
                hex.push_back(digits[digest[i] & 0x0f]);
            }
            CHECK_EQ(hex, identity.fingerprint);
        }

        const HrtDeviceIdentity second = generate_hrt_device_identity("hrt-tool test");
        CHECK(second.fingerprint != identity.fingerprint);
        CHECK_THROWS(generate_hrt_device_identity(std::string(65, 'a')), ToolError);
        CHECK_THROWS(generate_hrt_device_identity(""), ToolError);
        CHECK_THROWS(generate_hrt_device_identity("bad\nname"), ToolError);
    }

    {
        const std::string code = generate_hrt_code();
        CHECK_EQ(code.size(), static_cast<size_t>(128));
        CHECK(all_hex(code));
        CHECK(generate_hrt_code() != code);
        const std::string hash = sha256_hex(code);
        CHECK_EQ(hash.size(), static_cast<size_t>(64));
        CHECK(all_hex(hash));
    }

    {
        const std::string binary("\x00\x01\x02hello\xff", 9);
        const std::string encoded = base64_encode(binary);
        CHECK_EQ(base64_decode(encoded), binary);
        CHECK_EQ(base64_encode(""), std::string());
        CHECK_THROWS(base64_decode("!!!"), ToolError);
    }

    {
        CHECK(is_hrt_url("hrt://example.com"));
        CHECK(is_hrt_url("HRT://example.com:9443/"));
        CHECK(!is_hrt_url("https://example.com"));
        CHECK_EQ(hrt_url_host("hrt://example.com:9443/"), std::string("example.com"));
        CHECK_EQ(hrt_url_port("hrt://example.com:9443/"), std::string("9443"));
        CHECK_EQ(hrt_url_port("hrt://example.com/"), std::string("9443"));
        CHECK_EQ(hrt_https_fallback("hrt://example.com:9443"), std::string("https://example.com"));
        CHECK_EQ(hrt_https_fallback("hrt://127.0.0.1:9443"), std::string("https://127.0.0.1"));
        CHECK_THROWS(hrt_url_host("hrt://"), ToolError);
        CHECK_THROWS(hrt_url_port("hrt://example.com:99999"), ToolError);
        CHECK_THROWS(HrtClient("hrt://127.0.0.1:9443/path", HrtClientOptions{}),
                     ToolError);
    }

    {
        const HrtDeviceIdentity identity =
            generate_hrt_device_identity("hrt-sign-test");
        const std::map<std::string, std::string> headers{
            {"authorization", "Bearer abc"}, {"if-match", "rev-1"}};
        const std::string body = "{\"data\":1}";
        const std::string canonical =
            hrt_request_signing_string("PUT", "/api/tool/v1/snapshot", body, headers);
        CHECK_EQ(canonical, std::string("hrt-request-v1\nPUT\n/api/tool/v1/snapshot\n") +
                                sha256_hex(body) + "\nBearer abc\nrev-1");

        const std::string signature =
            hrt_sign_request(identity.private_key_pem, canonical);
        CHECK(!signature.empty());
        const std::string der = base64_decode(signature);

        std::unique_ptr<BIO, BioDeleter> cert_bio(
            BIO_new_mem_buf(identity.certificate_pem.data(),
                            static_cast<int>(identity.certificate_pem.size())));
        std::unique_ptr<X509, CertDeleter> cert(
            cert_bio ? PEM_read_bio_X509(cert_bio.get(), nullptr, nullptr, nullptr)
                     : nullptr);
        CHECK(cert != nullptr);
        if (cert) {
            std::unique_ptr<EVP_PKEY, KeyDeleter> public_key(
                X509_get_pubkey(cert.get()));
            CHECK(public_key != nullptr);
            if (public_key) {
                std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
                    EVP_MD_CTX_new(), EVP_MD_CTX_free);
                CHECK(context != nullptr);
                if (context) {
                    CHECK_EQ(EVP_DigestVerifyInit(context.get(), nullptr, EVP_sha256(),
                                                  nullptr, public_key.get()),
                             1);
                    CHECK_EQ(EVP_DigestVerify(
                                 context.get(),
                                 reinterpret_cast<const unsigned char *>(der.data()),
                                 der.size(),
                                 reinterpret_cast<const unsigned char *>(canonical.data()),
                                 canonical.size()),
                             1);
                    std::string tampered = canonical;
                    tampered[0] = 'X';
                    EVP_MD_CTX_reset(context.get());
                    CHECK_EQ(EVP_DigestVerifyInit(context.get(), nullptr, EVP_sha256(),
                                                  nullptr, public_key.get()),
                             1);
                    CHECK(EVP_DigestVerify(
                              context.get(),
                              reinterpret_cast<const unsigned char *>(der.data()),
                              der.size(),
                              reinterpret_cast<const unsigned char *>(tampered.data()),
                              tampered.size()) != 1);
                }
            }
        }

        CHECK_THROWS(hrt_request_signing_string(
                         "GET", "/x", "", {{"authorization", "a\nb"}}),
                     ToolError);
        CHECK_THROWS(hrt_request_signing_string("GET", "missing-slash", "", {}),
                     ToolError);
        CHECK_THROWS(hrt_request_signing_string("", "/x", "", {}), ToolError);
    }

    return test_result();
}
