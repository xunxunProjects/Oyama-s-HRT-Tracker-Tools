#include "api.h"

#include <utility>

namespace {

class StubApiClient : public ApiClient {
public:
    explicit StubApiClient(std::string server) : server_(std::move(server)) {}

    LoginResult login(const std::string &, const std::string &, const std::string &,
                      const std::string &, const std::string &) override {
        not_implemented("login");
    }

    void logout() override { not_implemented("logout"); }

    nlohmann::json me() override { not_implemented("me"); }

    std::optional<Snapshot> latest_snapshot() override {
        not_implemented("latest snapshot");
    }

    Snapshot put_snapshot(const nlohmann::json &, const std::string &) override {
        not_implemented("put snapshot");
    }

    nlohmann::json server_version() override {
        return nlohmann::json{
            {"apiVersion", 1},
            {"transport", {"tls", "openssl-v1"}},
        };
    }

private:
    [[noreturn]] static void not_implemented(const std::string &what) {
        throw ToolError(ExitCode::NotImplemented, "NOT_IMPLEMENTED",
                        what + " is not implemented in M1");
    }

    std::string server_;
};

} // namespace

std::unique_ptr<ApiClient> make_api_client(const std::string &server,
                                           const HttpOptions &http_options) {
    (void)http_options;
    return std::make_unique<StubApiClient>(server);
}
