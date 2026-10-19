#include "api.h"
#include "error.h"
#include "test_framework.h"

#include <nlohmann/json.hpp>
#include <string>

int main() {
    {
        const LoginResult login = parse_login_response(nlohmann::json{
            {"token", "jwt"},
            {"user", {{"id", "user-1"}, {"username", "Alice"}}},
            {"devicePass", "device"},
            {"apiVersion", 1},
        });
        CHECK_EQ(login.token, std::string("jwt"));
        CHECK_EQ(login.user_id, std::string("user-1"));
        CHECK_EQ(login.username, std::string("Alice"));
        CHECK_EQ(login.device_pass, std::string("device"));
        CHECK_EQ(login.api_version, 1);
    }

    {
        bool threw = false;
        try {
            (void)parse_login_response(nlohmann::json{{"user", nlohmann::json::object()}});
        } catch (const ToolError &error) {
            threw = true;
            CHECK_EQ(static_cast<int>(error.exit_code()), 5);
        }
        CHECK(threw);
    }

    {
        const Snapshot snapshot = parse_snapshot_response(nlohmann::json{
            {"id", "rev-1"},
            {"createdAt", 1792435000},
            {"data", {{"cloud", 1}, {"iv", "iv"}, {"data", "ct"}}},
        });
        CHECK_EQ(snapshot.id, std::string("rev-1"));
        CHECK_EQ(snapshot.created_at, static_cast<int64_t>(1792435000));
        CHECK_EQ(snapshot.data.at("cloud").get<int>(), 1);
    }

    {
        bool threw = false;
        try {
            (void)parse_snapshot_response(nlohmann::json{{"id", "rev-1"}});
        } catch (const ToolError &error) {
            threw = true;
            CHECK_EQ(static_cast<int>(error.exit_code()), 5);
        }
        CHECK(threw);
    }

    {
        const auto [id, created_at] = parse_conflict_current(nlohmann::json{
            {"code", "CONFLICT"},
            {"current", {{"id", "abc-123"}, {"createdAt", 42}}},
        });
        CHECK_EQ(id, std::string("abc-123"));
        CHECK_EQ(created_at, static_cast<int64_t>(42));
        const auto missing = parse_conflict_current(nlohmann::json{{"code", "CONFLICT"}});
        CHECK(missing.first.empty());
    }

    {
        CHECK(valid_bearer_token("eyJhbGci.eyJzdWIi.sig"));
        CHECK(!valid_bearer_token("bad token\r\nInjected: 1"));
        CHECK(!valid_bearer_token(""));
        CHECK(valid_snapshot_id("5f0bf66b-a17d-49eb-8e06-dfb426b1c148"));
        CHECK(!valid_snapshot_id("../logout"));
        CHECK(!valid_snapshot_id("a/b"));
    }

    {
        bool auth = false;
        try {
            throw_http_status(401, R"({"code":"SESSION_INVALID","message":"x"})", "me");
        } catch (const ApiAuthError &error) {
            auth = true;
            CHECK_EQ(error.code(), std::string("SESSION_INVALID"));
        }
        CHECK(auth);
    }

    {
        bool redirect = false;
        try {
            throw_http_status(307, "", "login");
        } catch (const ToolError &error) {
            redirect = error.code() == "REDIRECT" &&
                       error.exit_code() == ExitCode::Network;
        }
        CHECK(redirect);
    }

    {
        bool network = false;
        try {
            throw_http_status(500, "not json", "version");
        } catch (const ToolError &error) {
            network = error.code() == "HTTP_500" &&
                      error.exit_code() == ExitCode::Network;
        }
        CHECK(network);
    }

    {
        bool rejected = false;
        try {
            (void)parse_login_response(nlohmann::json{
                {"token", "bad\r\ntoken"},
                {"user", {{"id", "user-1"}, {"username", "Alice"}}},
            });
        } catch (const ToolError &error) {
            rejected = error.code() == "LOGIN_RESPONSE";
        }
        CHECK(rejected);
    }

    return test_result();
}
