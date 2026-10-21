#include "error.h"
#include "http.h"
#include "test_framework.h"

#include <sstream>

int main() {
    std::ostringstream note;
    CHECK_EQ(normalize_proxy_url("socks5://127.0.0.1:9050/", &note),
             std::string("socks5h://127.0.0.1:9050"));
    CHECK(note.str().find("socks5h") != std::string::npos);
    CHECK_EQ(normalize_proxy_url("socks5h://127.0.0.1:4447", nullptr),
             std::string("socks5h://127.0.0.1:4447"));
    CHECK_THROWS(normalize_proxy_url("http://127.0.0.1:8080", nullptr), ToolError);
    CHECK_THROWS(normalize_proxy_url("socks4://127.0.0.1:9050", nullptr), ToolError);
    CHECK_EQ(normalize_proxy_url("socks5://127.0.0.1", nullptr),
             std::string("socks5h://127.0.0.1:1080"));
    return test_result();
}
