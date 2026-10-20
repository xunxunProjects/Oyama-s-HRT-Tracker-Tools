#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

/* A random, per-install device identity. The private key is stored only in
   the encrypted vault; the self-signed certificate is uploaded during
   pairing so the web app can register its SHA-256 DER fingerprint. */
struct HrtDeviceIdentity {
    std::string private_key_pem;
    std::string certificate_pem;
    std::string fingerprint; /* lowercase SHA-256 hex of the DER certificate */
};

HrtDeviceIdentity generate_hrt_device_identity(const std::string &device_name);

/* 64 random bytes printed as 128 lowercase hex characters. */
std::string generate_hrt_code();
std::string sha256_hex(const std::string &data);
std::string base64_encode(const std::string &data);
std::string base64_decode(const std::string &data);

struct HrtServerCertificate {
    std::string spki_sha256;
    std::string subject;
    std::string issuer;
    std::string not_before;
    std::string not_after;
};

struct HrtClientOptions {
    std::string proxy;             /* socks5h://host:port, or empty */
    /* Device certificate/key. The certificate travels in authenticated
       request frames; the key signs them. Neither is used for TLS. */
    std::string client_cert_pem;
    std::string client_key_pem;
    std::string expected_spki;     /* empty only for the probe */
    long timeout_s = 30;
};

/* Canonical bytes signed by the device key for an authenticated request:
       hrt-request-v1\nMETHOD\nPATH\nsha256_hex(body)\nAUTHORIZATION\nIF_MATCH
   Header names are lowercase; missing headers contribute an empty string.
   Throws on control characters so a header cannot inject a new field. */
std::string hrt_request_signing_string(
    const std::string &method, const std::string &path, const std::string &body,
    const std::map<std::string, std::string> &headers);
/* ECDSA-SHA256 over the canonical bytes, returned as base64 DER. */
std::string hrt_sign_request(const std::string &private_key_pem,
                             const std::string &canonical);

/* A short-lived hrt/1 connection over TLS 1.3. The server certificate is
   authenticated by the stored SPKI pin, not by a public CA. */
class HrtClient {
public:
    HrtClient(std::string url, HrtClientOptions options);
    ~HrtClient();
    HrtClient(const HrtClient &) = delete;
    HrtClient &operator=(const HrtClient &) = delete;

    /* TLS handshake only; useful for the first-contact certificate display.
       The pin is checked when expected_spki is set. */
    HrtServerCertificate connect();
    void close();

    nlohmann::json call(const nlohmann::json &message);
    nlohmann::json request(const std::string &method, const std::string &path,
                           const std::map<std::string, std::string> &headers,
                           const std::string &body);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/* hrt://host[:port][/] — returns the host and an explicit/default port. */
std::string hrt_url_host(const std::string &url);
std::string hrt_url_port(const std::string &url); /* "9443" when omitted */
bool is_hrt_url(const std::string &url);
std::string hrt_https_fallback(const std::string &url);
