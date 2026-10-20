#include "completion.h"
#include "error.h"
#include "test_framework.h"

#include <sstream>

int main() {
    std::ostringstream bash, zsh, fish;
    CHECK_EQ(render_completion("bash", bash), 0);
    CHECK_EQ(render_completion("zsh", zsh), 0);
    CHECK_EQ(render_completion("fish", fish), 0);
    CHECK(bash.str().find("hrt-tool") != std::string::npos);
    CHECK_THROWS(render_completion("tcsh", bash), ToolError);
    return test_result();
}
