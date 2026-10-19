#include "api.h"
#include "error.h"
#include "test_framework.h"

#include <nlohmann/json.hpp>
#include <string>

namespace {

template <typename Function>
void check_not_implemented(Function &&function) {
    try {
        function();
        std::cerr << "expected ToolError, but the call returned\n";
        ++g_failures;
    } catch (const ToolError &error) {
        CHECK_EQ(static_cast<int>(error.exit_code()), 6);
        CHECK(std::string(error.what()).find("not implemented in M1") != std::string::npos);
    } catch (...) {
        std::cerr << "expected ToolError, but a different exception was thrown\n";
        ++g_failures;
    }
}

} // namespace

int main() {
    HttpOptions http;
    std::unique_ptr<ApiClient> client = make_api_client("not a url", http);
    CHECK(client != nullptr);

    const nlohmann::json version = client->server_version();
    CHECK_EQ(version.value("apiVersion", 0), 1);
    CHECK(version.contains("transport"));

    const nlohmann::json transport = version.at("transport");
    CHECK_EQ(transport.size(), static_cast<size_t>(2));
    CHECK_EQ(transport.at(0).get<std::string>(), std::string("tls"));
    CHECK_EQ(transport.at(1).get<std::string>(), std::string("openssl-v1"));

    check_not_implemented([&] { client->login("user", "password", "", "", ""); });
    check_not_implemented([&] { client->logout(); });
    check_not_implemented([&] { client->me(); });
    check_not_implemented([&] { client->latest_snapshot(); });
    check_not_implemented(
        [&] { client->put_snapshot(nlohmann::json::object(), std::string{}); });

    return test_result();
}
