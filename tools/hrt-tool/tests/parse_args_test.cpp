#include "cli.h"
#include "test_framework.h"

#include <string>
#include <vector>

static ParsedArgs parse(std::vector<const char *> args) {
    args.insert(args.begin(), "hrt-tool");
    return parse_cli_args(static_cast<int>(args.size()), args.data());
}

int main() {
    ParsedArgs a = parse({"add", "dose", "--debug"});
    CHECK_EQ(a.command, std::string("add"));
    CHECK_EQ(a.command_args.size(), size_t{1});
    CHECK_EQ(a.command_args[0], std::string("dose"));
    CHECK(a.options.debug);

    ParsedArgs b = parse({"status", "--json"});
    CHECK(b.options.json);
    CHECK_EQ(b.command, std::string("status"));

    ParsedArgs c = parse({"f2f", "login", "--server", "https://x",
                          "--token-file", "t", "--help"});
    CHECK_EQ(c.command, std::string("f2f"));
    CHECK_EQ(c.command_args.size(), size_t{6});
    CHECK_EQ(c.command_args[0], std::string("login"));
    CHECK_EQ(c.command_args[1], std::string("--server"));
    CHECK_EQ(c.command_args[2], std::string("https://x"));
    CHECK_EQ(c.command_args[5], std::string("--help"));
    CHECK(!c.options.server_explicit);
    CHECK(!c.help);
    return test_result();
}
