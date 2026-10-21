#include "f2f_bridge.h"
#include "hrtcore/hrtcore.h"
#include "hrtprint/hrtprint.h"
#include "version.h"

#include <cstring>
#include <iostream>
#include <string>

static int failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond     \
                      << "\n";                                                      \
            ++failures;                                                             \
        }                                                                           \
    } while (0)

int main() {
    CHECK(std::strcmp(hrtcore_version(), "0.1.1") == 0);
    CHECK(hrtcore_abi_version() == 1u);
    CHECK(std::strcmp(hrtprint_version(), "0.1.1") == 0);
    CHECK(current_version_string() == "0.1.1");
    CHECK(format_user_agent() == "hrt-tool/0.1.1");
#if HRT_TOOL_F2F_ENABLED
    CHECK(f2f_version() == "0.1.1");
#endif

    ToolVersion v = current_version();
    CHECK(v.major == 0);
    CHECK(v.minor == 1);
    CHECK(v.patch == 1);

    if (failures == 0) {
        std::cout << "version_test: PASS\n";
    }
    return failures == 0 ? 0 : 1;
}
