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
    CHECK_EQ(parsed["tool"], "0.1.1");
    CHECK_EQ(parsed["core"], "0.1.1");
    CHECK_EQ(parsed["print"], "0.1.1");
#if HRT_TOOL_F2F_ENABLED
    CHECK_EQ(parsed["f2f"], "0.1.1");
#else
    CHECK_EQ(parsed["f2f"], "disabled");
#endif
    return test_result();
}
