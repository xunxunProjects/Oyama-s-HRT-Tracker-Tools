#pragma once

#include <map>
#include <ostream>
#include <string>
#include <vector>

struct Url {
    std::string scheme;
    std::string host;
    std::string port;
    std::string path;
};

Url parse_url(const std::string &url);
void validate_server_url(const Url &url);
void validate_proxy_url(const std::string &proxy);

struct HttpResponse {
    long status = 0;
    std::string body;
    std::multimap<std::string, std::string> headers;
    std::string spki_sha256;
    std::string cert_subject;
    std::string cert_issuer;
    std::string cert_not_before;
    std::string cert_not_after;
};

bool is_onion_host(const std::string &host);
bool is_i2p_host(const std::string &host);
bool valid_onion_v3(const std::string &host);
bool valid_i2p_host(const std::string &host);
/* The conventional local proxy for this hidden-service suffix, or empty. */
std::string suggested_proxy_for_host(const std::string &host);

struct HttpOptions {
    std::string proxy;
    /* Hex SPKI SHA-256. When set, libcurl refuses the TLS handshake unless the
       server key matches, before any request body is sent. */
    std::string pinned_spki_sha256;
    bool debug = false;
    bool trace = false;
    long timeout_s = 30;
    bool insecure_tls = false;
    std::ostream *trace_out = nullptr;
};

std::string redact_header_line(const std::string &header_line);
std::string redact_json_secrets(const std::string &json);

class HttpClient {
public:
    HttpResponse request(const std::string &method, const std::string &url,
                         const std::vector<std::string> &headers, const std::string &body,
                         const HttpOptions &options);
};
