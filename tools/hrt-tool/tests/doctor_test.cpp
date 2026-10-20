#include "commands.h"
#include "session.h"
#include "test_framework.h"
#include "vault.h"

#include <filesystem>
#include <fstream>
#include <sstream>

int main() {
    const auto dir = std::filesystem::temp_directory_path() / "hrt-doctor-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    Vault::create(dir / "vault.hrtv", "correct horse battery staple");
    std::ofstream(dir / "pass") << "correct horse battery staple\n";
    std::filesystem::permissions(
        dir / "pass", std::filesystem::perms::owner_read |
                          std::filesystem::perms::owner_write);

    GlobalOptions options;
    options.config_dir = dir.string();
    options.vault_password_file = (dir / "pass").string();
    SessionContext context;
    std::istringstream in;
    std::ostringstream out, err;
    UnlockedVault u = unlock_full(options, in, out, &context);
    u.session().data.token = "SECRET-TOKEN";

    ParsedArgs parsed;
    parsed.options = options;
    parsed.command = "doctor";
    CHECK_EQ(command_doctor(parsed, in, out, err, &context), 0);
    CHECK(out.str().find("PASS") != std::string::npos);
    CHECK(out.str().find("SECRET-TOKEN") == std::string::npos);
    CHECK(out.str().find("correct horse") == std::string::npos);
    std::filesystem::remove_all(dir);
    return test_result();
}
