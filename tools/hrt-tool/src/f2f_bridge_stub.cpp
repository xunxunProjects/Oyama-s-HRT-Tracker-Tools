#include "f2f_bridge.h"

#include "error.h"

int run_f2f(const std::vector<std::string> &args, std::ostream &out,
            std::ostream &err) {
    (void)args;
    (void)out;
    err << "F2F support is disabled in this build [F2F_DISABLED]\n";
    return static_cast<int>(ExitCode::Platform);
}

std::string f2f_version() { return "disabled"; }
