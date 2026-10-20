#include "version.h"

#include "f2f_bridge.h"
#include "hrtcore/hrtcore.h"
#include "hrtprint/hrtprint.h"

#include <nlohmann/json.hpp>

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
    out << "f2f " << f2f_version() << '\n';
    return out.str();
}

std::string format_version_json() {
    const ToolVersion version = current_version();
    const std::string tool = std::to_string(version.major) + "." +
                             std::to_string(version.minor) + "." +
                             std::to_string(version.patch);
    return nlohmann::json{
        {"tool", tool},
        {"core", hrtcore_version()},
        {"print", hrtprint_version()},
        {"f2f", f2f_version()},
    }
        .dump(2);
}
