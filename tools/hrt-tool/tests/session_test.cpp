#include "commands.h"
#include "session.h"
#include "test_framework.h"
#include "vault.h"

#include <filesystem>
#include <iostream>
#include <sstream>

int main() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "hrt-session-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path vault = dir / "vault.hrtv";
    Vault::create(vault, "correct horse battery staple");

    GlobalOptions options;
    options.config_dir = dir.string();
    SessionContext context;
    std::istringstream first("correct horse battery staple\n");
    {
        UnlockedVault u = unlock_full(options, first, std::cout, &context);
        CHECK(context.loaded);
        CHECK_EQ(u.session().data.version, 2);
        u.session().data.language = "ru";
    }
    std::istringstream second("");
    std::ostringstream prompt;
    {
        UnlockedVault u = unlock_full(options, second, prompt, &context);
        CHECK_EQ(u.session().data.language, std::string("ru"));
        CHECK(prompt.str().find("password") == std::string::npos);
    }

    ParsedArgs parsed;
    parsed.options.config_dir = dir.string();
    parsed.command = "config";
    parsed.command_args = {"get", "language"};
    std::istringstream no_input("");
    std::ostringstream out;
    std::ostringstream err;
    CHECK_EQ(dispatch(parsed, no_input, out, err, &context), 0);
    CHECK(out.str().find("language: ru") != std::string::npos);
    CHECK(err.str().find("password") == std::string::npos);
    std::filesystem::remove_all(dir);
    return test_result();
}
