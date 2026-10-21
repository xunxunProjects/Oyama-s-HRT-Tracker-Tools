#include "commands.h"
#include "session.h"
#include "test_framework.h"
#include "vault.h"

#include <filesystem>
#include <fstream>
#include <sstream>

int main() {
    const auto dir =
        std::filesystem::temp_directory_path() / "hrt-proxy-cmd-test";
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

    ParsedArgs tor;
    tor.options = options;
    tor.command = "proxy";
    tor.command_args = {"tor"};
    CHECK_EQ(command_proxy(tor, in, out, err, &context), 0);
    CHECK_EQ(context.session.data.proxy,
             std::string("socks5h://127.0.0.1:9050"));

    ParsedArgs off;
    off.options = options;
    off.command = "proxy";
    off.command_args = {"off"};
    CHECK_EQ(command_proxy(off, in, out, err, &context), 0);
    CHECK(context.session.data.proxy.empty());
    std::filesystem::remove_all(dir);
    return test_result();
}
