#include "cli.h"
#include "test_framework.h"

#include <nlohmann/json.hpp>
#include <sstream>

int main() {
    const char *argv[] = {"hrt-tool", "version", "--json"};
    std::istringstream in;
    std::ostringstream out, err;
    CHECK_EQ(run_cli(3, argv, in, out, err), 0);
    const nlohmann::json parsed = nlohmann::json::parse(out.str());
    CHECK(parsed.contains("tool"));
    return test_result();
}
