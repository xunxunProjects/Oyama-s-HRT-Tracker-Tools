#include "version.h"

#include "hrtcore/hrtcore.h"
#include "hrtprint/hrtprint.h"

#include <sstream>

ToolVersion current_version() {
    return ToolVersion{0, 1, 0};
}

std::string format_version_line() {
    const ToolVersion version = current_version();
    std::ostringstream out;
    out << "hrt-tool " << version.major << '.' << version.minor << '.' << version.patch << '\n';
    out << "hrtcore " << hrtcore_version() << " (ABI " << hrtcore_abi_version() << ")\n";
    out << "hrtprint " << hrtprint_version() << '\n';
    return out.str();
}
