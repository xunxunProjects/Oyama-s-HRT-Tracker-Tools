#include "cli.h"
#include "error.h"
#include "test_framework.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_counter = 0;

class TempDir {
public:
    TempDir() {
        path_ = std::filesystem::temp_directory_path() /
                ("hrt-link-commands-" + std::to_string(++g_counter));
        std::filesystem::create_directories(path_);
        const std::filesystem::path pass = path_ / "pass";
        std::ofstream(pass) << "correct horse battery staple\n";
        std::filesystem::permissions(
            pass, std::filesystem::perms::owner_read |
                      std::filesystem::perms::owner_write);
        pass_ = pass.string();
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path &path() const { return path_; }
    const std::string &pass() const { return pass_; }

private:
    std::filesystem::path path_;
    std::string pass_;
};

struct RunResult {
    int code = 0;
    std::string out;
    std::string err;
};

RunResult run(const std::string &config_dir, const std::string &pass,
              const std::vector<std::string> &args) {
    std::vector<std::string> full = args;
    full.push_back("--config-dir");
    full.push_back(config_dir);
    full.push_back("--vault-password-file");
    full.push_back(pass);
    std::vector<const char *> argv;
    argv.push_back("hrt-tool");
    for (const std::string &arg : full) {
        argv.push_back(arg.c_str());
    }
    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    const int code = run_cli(static_cast<int>(argv.size()), argv.data(), in, out, err);
    return RunResult{code, out.str(), err.str()};
}

bool contains(const std::string &text, const std::string &needle) {
    return text.find(needle) != std::string::npos;
}

} // namespace

int main() {
    TempDir temp;
    CHECK_EQ(run(temp.path().string(), temp.pass(), {"init"}).code, 0);

    RunResult bad_key =
        run(temp.path().string(), temp.pass(), {"config", "set", "bogus", "x"});
    CHECK_EQ(bad_key.code, static_cast<int>(ExitCode::Config));
    CHECK(contains(bad_key.err, "CONFIG_KEY"));

    RunResult reserved = run(temp.path().string(), temp.pass(),
                             {"config", "set", "transport", "tls"});
    CHECK_EQ(reserved.code, static_cast<int>(ExitCode::NotImplemented));
    CHECK(contains(reserved.err, "TRANSPORT_SELECTION"));

    RunResult probe = run(temp.path().string(), temp.pass(),
                          {"add-link", "http://127.0.0.1:1", "probe", "--probe"});
    CHECK(probe.code == static_cast<int>(ExitCode::Network) ||
          probe.code == static_cast<int>(ExitCode::Trust));
    CHECK(probe.err.find("[URL]") == std::string::npos);

    RunResult added =
        run(temp.path().string(), temp.pass(),
            {"add-link", "hrt://127.0.0.1:9443", "mtf", "--mode", "transfem"});
    CHECK_EQ(added.code, 0);
    CHECK(contains(added.out, "link added: mtf"));

    RunResult committed =
        run(temp.path().string(), temp.pass(), {"commit-server", "mtf"});
    CHECK_EQ(committed.code, 0);
    CHECK(contains(committed.out, "active link: mtf"));

    RunResult listed = run(temp.path().string(), temp.pass(), {"link", "list"});
    CHECK_EQ(listed.code, 0);
    CHECK(contains(listed.out, "mtf"));
    CHECK(contains(listed.out, "active"));

    RunResult shown =
        run(temp.path().string(), temp.pass(), {"link", "show", "mtf"});
    CHECK_EQ(shown.code, 0);
    CHECK(contains(shown.out, "token: empty"));

    RunResult active = run(temp.path().string(), temp.pass(),
                           {"config", "get", "active-link"});
    CHECK_EQ(active.code, 0);
    CHECK(contains(active.out, "active-link: mtf"));

    RunResult mode = run(temp.path().string(), temp.pass(),
                         {"config", "get", "mode"});
    CHECK_EQ(mode.code, 0);
    CHECK(contains(mode.out, "mode: transfem"));

    RunResult active_remove = run(temp.path().string(), temp.pass(),
                                  {"link", "remove", "mtf", "--yes"});
    CHECK_EQ(active_remove.code, static_cast<int>(ExitCode::Conflict));
    CHECK(contains(active_remove.err, "LINK_ACTIVE"));

    /* Replacing the active link must not silently keep the old session or
       print success while the old record is written back. */
    RunResult forced = run(temp.path().string(), temp.pass(),
                           {"add-link", "https://b.example", "mtf", "--force"});
    CHECK_EQ(forced.code, 0);
    RunResult forced_show = run(temp.path().string(), temp.pass(),
                                {"link", "show", "mtf"});
    CHECK(contains(forced_show.out, "https://b.example"));
    CHECK(contains(forced_show.out, "token: empty"));
    RunResult forced_active = run(temp.path().string(), temp.pass(),
                                  {"config", "get", "active-link"});
    CHECK(contains(forced_active.out, "active-link: (base)"));

    RunResult renamed = run(temp.path().string(), temp.pass(),
                            {"link", "rename", "mtf", "main"});
    CHECK_EQ(renamed.code, 0);
    CHECK(contains(renamed.out, "main"));

    RunResult removed = run(temp.path().string(), temp.pass(),
                            {"link", "remove", "main", "--yes"});
    CHECK_EQ(removed.code, 0);
    CHECK(contains(removed.out, "link removed: main"));
    return test_result();
}
