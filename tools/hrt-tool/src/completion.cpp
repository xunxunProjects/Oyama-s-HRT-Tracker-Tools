#include "completion.h"

#include "error.h"

namespace {

const char *kCommands =
    "version init login logout whoami console look status add add-link "
    "commit-server link dose lab weight export import snapshot config logs "
    "doctor proxy completion help f2f";

const char *kFlags =
    "--server --proxy --config-dir --token-file --debug --trace --json "
    "--no-color --yes --force --probe --no-humor --timeout --trust-server "
    "--insecure-tls --transport --compute --vault-password-file "
    "--vault-password-stdin --log-file --user-id --account-password-file "
    "--account-password-stdin --snapshot --mode --utc --device-name "
    "--require-hrt --no-cache-cloud-key";

} // namespace

int render_completion(const std::string &shell, std::ostream &out) {
    if (shell == "bash") {
        out << "# hrt-tool bash completion\n"
            << "complete -W \"" << kCommands << " " << kFlags << "\" hrt-tool\n";
        return 0;
    }
    if (shell == "zsh") {
        out << "#compdef hrt-tool\n"
            << "_hrt_tool() {\n"
            << "  local -a commands\n"
            << "  commands=(" << kCommands << ")\n"
            << "  _describe 'command' commands\n"
            << "}\n"
            << "compdef _hrt_tool hrt-tool\n";
        return 0;
    }
    if (shell == "fish") {
        out << "# hrt-tool fish completion\n"
            << "complete -c hrt-tool -f -a \"" << kCommands << "\"\n";
        return 0;
    }
    throw ToolError(ExitCode::Usage, "COMPLETION",
                    "completion needs bash, zsh, or fish");
}
