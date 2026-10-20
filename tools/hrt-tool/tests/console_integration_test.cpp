#include "console.h"
#include "test_framework.h"
#include "vault.h"

#include <filesystem>
#include <sstream>

int main() {
    const auto dir =
        std::filesystem::temp_directory_path() / "hrt-console-integration";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    Vault::create(dir / "vault.hrtv", "correct horse battery staple");

    GlobalOptions options;
    options.config_dir = dir.string();
    std::istringstream in(
        "correct horse battery staple\n"
        "add-link hrt://127.0.0.1:9443 mtf\n"
        "commit-server mtf\n"
        "version\n"
        "exit\n"
        "exit\n");
    std::ostringstream out, err;
    CHECK_EQ(run_console(options, in, out, err), 0);
    CHECK(out.str().find("hrt$mtf>") != std::string::npos);
    CHECK(out.str().find("hrt-tool") != std::string::npos);
    std::filesystem::remove_all(dir);
    return test_result();
}
