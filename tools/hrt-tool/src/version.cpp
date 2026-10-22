#include "version.h"

#include "f2f_bridge.h"
#include "hrtcore/hrtcore.h"
#include "hrtprint/hrtprint.h"

#include <nlohmann/json.hpp>

#include <sstream>

#ifndef HRT_TOOL_VERSION_MAJOR
#define HRT_TOOL_VERSION_MAJOR 0
#endif
#ifndef HRT_TOOL_VERSION_MINOR
#define HRT_TOOL_VERSION_MINOR 1
#endif
#ifndef HRT_TOOL_VERSION_PATCH
#define HRT_TOOL_VERSION_PATCH 1
#endif

ToolVersion current_version() {
    return ToolVersion{HRT_TOOL_VERSION_MAJOR, HRT_TOOL_VERSION_MINOR,
                       HRT_TOOL_VERSION_PATCH};
}

std::string current_version_string() {
    const ToolVersion version = current_version();
    return std::to_string(version.major) + "." + std::to_string(version.minor) +
           "." + std::to_string(version.patch);
}

std::string format_user_agent() { return "hrt-tool/" + current_version_string(); }

std::string format_version_line() {
    std::ostringstream out;
    out << "hrt-tool " << current_version_string() << '\n';
    out << "hrtcore " << hrtcore_version() << " (ABI " << hrtcore_abi_version() << ")\n";
    out << "hrtprint " << hrtprint_version() << '\n';
    out << "f2f " << f2f_version() << '\n';
    return out.str();
}

std::string format_version_json() {
    return nlohmann::json{
        {"tool", current_version_string()},
        {"core", hrtcore_version()},
        {"print", hrtprint_version()},
        {"f2f", f2f_version()},
    }
        .dump(2);
}
