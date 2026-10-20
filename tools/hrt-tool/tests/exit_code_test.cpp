#include "api.h"
#include "error.h"
#include "test_framework.h"

#include <string>

int main() {
    CHECK_EQ(static_cast<int>(ExitCode::Internal), 1);
    CHECK_EQ(static_cast<int>(ExitCode::Auth), 7);
    CHECK_EQ(static_cast<int>(ExitCode::Trust), 8);
    CHECK_EQ(static_cast<int>(ExitCode::Permission), 9);
    CHECK_EQ(static_cast<int>(ExitCode::Conflict), 10);
    CHECK_EQ(static_cast<int>(ExitCode::Cancelled), 11);
    CHECK_EQ(static_cast<int>(ExitCode::Config), 12);
    CHECK_EQ(static_cast<int>(ExitCode::F2F), 13);
    CHECK_EQ(static_cast<int>(ExitCode::Platform), 14);
    CHECK_EQ(std::string(exit_code_name(ExitCode::Auth)), std::string("AUTH"));
    CHECK(ApiAuthError("NOT_LOGGED_IN", "x").exit_code() == ExitCode::Auth);
    CHECK(ApiTwoFactorRequired("totp").exit_code() == ExitCode::Auth);
    CHECK(SnapshotConflictError("id", 1).exit_code() == ExitCode::Conflict);
    return test_result();
}
