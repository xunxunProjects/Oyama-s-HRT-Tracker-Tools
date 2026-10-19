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
};

struct HttpOptions {
    std::string proxy;
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
