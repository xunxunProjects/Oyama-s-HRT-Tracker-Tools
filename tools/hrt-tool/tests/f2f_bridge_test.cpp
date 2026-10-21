#include "f2f_bridge.h"
#include "test_framework.h"

#include <sstream>

int main() {
    std::ostringstream out, err;
    CHECK_EQ(run_f2f({"version"}, out, err), 0);
    CHECK(out.str().find("hrt-f2f") != std::string::npos);
    std::ostringstream help_out, help_err;
    CHECK_EQ(run_f2f({"--help"}, help_out, help_err), 0);
    CHECK(help_out.str().find("keygen") != std::string::npos);
    CHECK(!f2f_version().empty());
    return test_result();
}
