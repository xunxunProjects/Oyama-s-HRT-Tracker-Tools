#include "paths.h"

#include "error.h"

#include <cstdlib>

Paths resolve_paths(const std::string &override_dir) {
    std::filesystem::path config_dir;

    if (!override_dir.empty()) {
        config_dir = override_dir;
    } else {
#ifdef _WIN32
        const char *appdata = std::getenv("APPDATA");
        if (appdata == nullptr || *appdata == '\0') {
            throw ToolError(ExitCode::Usage, "CONFIG_DIR",
                            "APPDATA is not set; pass --config-dir");
        }
        config_dir = std::filesystem::path(appdata) / "hrt-tool";
#else
        const char *xdg = std::getenv("XDG_CONFIG_HOME");
        if (xdg != nullptr && *xdg != '\0') {
            config_dir = std::filesystem::path(xdg) / "hrt-tool";
        } else {
            const char *home = std::getenv("HOME");
            if (home == nullptr || *home == '\0') {
                throw ToolError(ExitCode::Usage, "CONFIG_DIR",
                                "HOME is not set; pass --config-dir");
            }
            config_dir = std::filesystem::path(home) / ".config" / "hrt-tool";
        }
#endif
    }

    return Paths{config_dir, config_dir / "vault.hrtv"};
}
