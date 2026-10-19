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

    return test_result();
}
