#include "error.h"
#include "http.h"
#include "test_framework.h"

#include <string>

int main() {
    {
        const Url url = parse_url("https://hrt.mahiro.uk");
        CHECK_EQ(url.scheme, std::string("https"));
        CHECK_EQ(url.host, std::string("hrt.mahiro.uk"));
        CHECK_EQ(url.port, std::string("443"));
        CHECK_EQ(url.path, std::string("/"));
    }

    {
        const Url url = parse_url("https://example.com:8443/api?x=1");
        CHECK_EQ(url.scheme, std::string("https"));
        CHECK_EQ(url.host, std::string("example.com"));
        CHECK_EQ(url.port, std::string("8443"));
        CHECK_EQ(url.path, std::string("/api?x=1"));
    }

    {
        const Url url = parse_url("http://[::1]:8080/");
        CHECK_EQ(url.scheme, std::string("http"));
        CHECK_EQ(url.host, std::string("::1"));
        CHECK_EQ(url.port, std::string("8080"));
    }

    CHECK_THROWS(parse_url("not a url"), ToolError);
    CHECK_THROWS(parse_url("https://"), ToolError);
    CHECK_THROWS(parse_url("https://example.com:99999/"), ToolError);
    CHECK_THROWS(parse_url("https://user@example.com/"), ToolError);

    validate_server_url(parse_url("https://example.com"));
    validate_server_url(parse_url("http://127.0.0.1:8787"));
    validate_server_url(parse_url("http://localhost:8787"));
    validate_server_url(parse_url("http://abc.onion"));
    validate_server_url(parse_url("http://abc.i2p"));
    CHECK_THROWS(validate_server_url(parse_url("http://example.com")), ToolError);
    CHECK_THROWS(validate_server_url(parse_url("ftp://example.com")), ToolError);

    validate_proxy_url("socks5h://127.0.0.1:9050");
    validate_proxy_url("socks5://127.0.0.1:4447");
    validate_proxy_url("http://127.0.0.1:8080");
    validate_proxy_url("https://proxy.example:8443");
    CHECK_THROWS(validate_proxy_url("ftp://127.0.0.1:21"), ToolError);
    CHECK_THROWS(validate_proxy_url("socks5h://"), ToolError);

    {
        const std::string redacted = redact_header_line("Authorization: Bearer abc");
        CHECK(redacted.find("abc") == std::string::npos);
        CHECK(redacted.find("redacted") != std::string::npos);
        CHECK_EQ(redact_header_line("Content-Type: application/json"),
                 std::string("Content-Type: application/json"));

        const std::string json =
            redact_json_secrets("{\"token\":\"abc\",\"password\":\"def\",\"x\":\"y\"}");
        CHECK(json.find("abc") == std::string::npos);
        CHECK(json.find("def") == std::string::npos);
        CHECK(json.find("\"y\"") != std::string::npos);
    }

    {
        HttpClient client;
        HttpOptions options;
        CHECK_THROWS(client.request("GET", "http://example.com/", {}, "", options), ToolError);
    }

    return test_result();
}
