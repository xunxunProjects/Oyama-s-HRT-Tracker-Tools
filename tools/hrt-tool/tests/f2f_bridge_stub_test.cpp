#include "error.h"
#include "f2f_bridge.h"
#include "test_framework.h"

#include <sstream>

int main() {
    std::ostringstream out, err;
    CHECK_EQ(run_f2f({"version"}, out, err),
             static_cast<int>(ExitCode::Platform));
    CHECK(err.str().find("F2F_DISABLED") != std::string::npos);
    CHECK_EQ(f2f_version(), std::string("disabled"));
    return test_result();
}
