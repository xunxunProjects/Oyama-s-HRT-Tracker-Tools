#include "console.h"
#include "test_framework.h"
#include "vault.h"

#include <filesystem>
#include <sstream>
#include <string>

int main() {
    std::string error;
    std::vector<std::string> tokens =
        tokenize_console_line("add dose --drug \"Estradiol valerate\"", &error);
    CHECK_EQ(tokens.size(), size_t{4});
    CHECK_EQ(tokens[3], std::string("Estradiol valerate"));
    CHECK(tokenize_console_line("add 'unterminated", &error).empty());
    CHECK(!error.empty());

    tokens = tokenize_console_line("proxy socks5://127.0.0.1:9050/ # tor", &error);
    CHECK_EQ(tokens.size(), size_t{2});
    CHECK_EQ(tokens[0], std::string("proxy"));
    CHECK_EQ(tokens[1], std::string("socks5://127.0.0.1:9050/"));

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "hrt-console-test";
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
    std::ostringstream out;
    std::ostringstream err;
    CHECK_EQ(run_console(options, in, out, err), 0);
    CHECK(out.str().find("hrt>") != std::string::npos);
    CHECK(out.str().find("hrt$mtf>") != std::string::npos);
    CHECK(out.str().find("hrt-tool") != std::string::npos);
    CHECK(err.str().find("error") == std::string::npos);

    std::filesystem::remove_all(dir);
    return test_result();
}
