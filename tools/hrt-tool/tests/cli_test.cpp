#include "cli.h"
#include "test_framework.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

int g_temp_counter = 0;

class TempDir {
public:
    TempDir() {
        const char *base = std::getenv("TMPDIR");
        const std::filesystem::path root =
            base != nullptr && *base != '\0' ? std::filesystem::path(base)
                                             : std::filesystem::temp_directory_path();
        while (true) {
            path_ = root / ("hrt-tool-cli-test-" + std::to_string(++g_temp_counter));
            if (std::filesystem::create_directories(path_)) {
                break;
            }
        }
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;
    const std::filesystem::path &path() const { return path_; }

private:
    std::filesystem::path path_;
};

struct RunResult {
    int code;
    std::string out;
    std::string err;
};

RunResult run(const std::vector<std::string> &args, const std::string &input = "") {
    std::vector<const char *> argv;
    argv.push_back("hrt-tool");
    for (const std::string &arg : args) {
        argv.push_back(arg.c_str());
    }

    std::istringstream in(input);
    std::ostringstream out;
    std::ostringstream err;
    const int code = run_cli(static_cast<int>(argv.size()), argv.data(), in, out, err);
    return RunResult{code, out.str(), err.str()};
}

bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string read_text(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

const char *kPasswordLine = "correct horse battery staple\n";

} // namespace

int main() {
    {
        const RunResult result = run({"version"});
        CHECK_EQ(result.code, 0);
        CHECK(contains(result.out, "hrt-tool 0.1.0"));
        CHECK(contains(result.out, "hrtcore 0.1.0"));
        CHECK(contains(result.out, "hrtprint 0.1.0"));
    }

    {
        const RunResult result = run({"--help"});
        CHECK_EQ(result.code, 0);
        for (const char *word : {"init", "login", "look", "add", "export", "import",
                                 "snapshot", "config", "logs", "version"}) {
            CHECK(contains(result.out, word));
        }
    }

    {
        const RunResult result = run({"bogus"});
        CHECK_EQ(result.code, 2);
    }

    {
        TempDir temp;
        const RunResult result = run({"config", "path", "--config-dir", temp.path().string()});
        CHECK_EQ(result.code, 0);
        CHECK(contains(result.out, temp.path().string()));
        CHECK(contains(result.out, "vault.hrtv"));
        CHECK(!std::filesystem::exists(temp.path() / "vault.hrtv"));
    }

    {
        TempDir temp;
        const RunResult result = run(
            {"init", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(result.code, 0);
        CHECK(std::filesystem::exists(temp.path() / "vault.hrtv"));
        CHECK(contains(result.out, "guess"));
        CHECK(contains(result.out, "uniform"));
        CHECK(contains(result.out, "classes:"));
    }

    {
        TempDir temp;
        const RunResult result = run(
            {"init", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            "aaaaaaaaaaaaaaaa\n");
        CHECK_EQ(result.code, 3);
        CHECK(!std::filesystem::exists(temp.path() / "vault.hrtv"));
    }

    {
        TempDir temp;
        std::string long_password(300, 'a');
        long_password.push_back('\n');
        const RunResult result = run(
            {"init", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            long_password);
        CHECK_EQ(result.code, 5);
        CHECK(!std::filesystem::exists(temp.path() / "vault.hrtv"));
    }

    {
        TempDir temp;
        const std::filesystem::path password_file = temp.path() / "long.txt";
        {
            std::ofstream out(password_file);
            out << std::string(300, 'a') << "\n";
        }
#ifndef _WIN32
        ::chmod(password_file.c_str(), 0600);
#endif
        const RunResult result =
            run({"init", "--config-dir", temp.path().string(), "--vault-password-file",
                 password_file.string()});
        CHECK_EQ(result.code, 5);
        CHECK(!std::filesystem::exists(temp.path() / "vault.hrtv"));
    }

    {
        TempDir temp;
        const RunResult result = run(
            {"login", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(result.code, 3);
        CHECK(contains(result.err, "init"));
    }

    {
        TempDir temp;
        const RunResult init = run(
            {"init", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(init.code, 0);

        const auto with_vault = [&](std::vector<std::string> args) {
            args.push_back("--config-dir");
            args.push_back(temp.path().string());
            args.push_back("--vault-password-stdin");
            return args;
        };

        /* Live commands now reach the API; with no credentials/token they
           fail at the usage/auth boundary instead of the M1 stub. */
        CHECK_EQ(run(with_vault({"login"}), kPasswordLine).code, 2);
        const RunResult logout = run(with_vault({"logout"}), kPasswordLine);
        CHECK_EQ(logout.code, 0);
        CHECK(contains(logout.out, "not logged in"));
        CHECK_EQ(run(with_vault({"whoami"}), kPasswordLine).code, 3);
        CHECK_EQ(run(with_vault({"snapshot", "pull", "--out", "-"}), kPasswordLine).code, 3);

        const RunResult config_get = run(with_vault({"config", "get"}), kPasswordLine);
        CHECK_EQ(config_get.code, 6);
        CHECK(contains(config_get.err, "not implemented in M1"));
    }

    {
        TempDir temp;
        const RunResult init = run(
            {"init", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(init.code, 0);

        for (const char *command : {"look", "add", "dose", "lab", "weight", "export",
                                    "import"}) {
            std::vector<std::string> args{command};
            args.push_back("--config-dir");
            args.push_back(temp.path().string());
            args.push_back("--vault-password-stdin");
            const RunResult result = run(args, kPasswordLine);
            CHECK_EQ(result.code, 2);
            CHECK(contains(result.err, "--snapshot") ||
                  contains(result.err, "missing --out") ||
                  contains(result.err, "missing --in") ||
                  contains(result.err, "dose needs") ||
                  contains(result.err, "lab needs") ||
                  contains(result.err, "weight needs"));
        }

        const std::filesystem::path password_file = temp.path() / "password.txt";
        {
            std::ofstream out(password_file);
            out << kPasswordLine;
        }
#ifndef _WIN32
        ::chmod(password_file.c_str(), 0644);
#endif
        const RunResult result =
            run({"login", "--config-dir", temp.path().string(), "--vault-password-file",
                 password_file.string()});
        CHECK_EQ(result.code, 3);

        const RunResult missing_log =
            run({"logs", "show", "--config-dir", temp.path().string(),
                 "--vault-password-stdin"},
                kPasswordLine);
        CHECK_EQ(missing_log.code, 2);
    }

    {
        TempDir temp;
        const RunResult init = run(
            {"init", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(init.code, 0);

        const RunResult again = run(
            {"init", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(again.code, 3);

        const RunResult refused = run(
            {"init", "--reset", "--config-dir", temp.path().string(),
             "--vault-password-stdin"},
            "no\n" + std::string(kPasswordLine));
        CHECK_EQ(refused.code, 2);
        CHECK(std::filesystem::exists(temp.path() / "vault.hrtv"));

        const RunResult reset = run(
            {"init", "--reset", "--yes", "--config-dir", temp.path().string(),
             "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(reset.code, 0);
    }

    {
        TempDir temp;
        const RunResult init = run(
            {"init", "--config-dir", temp.path().string(), "--vault-password-stdin"},
            kPasswordLine);
        CHECK_EQ(init.code, 0);

        const std::filesystem::path log_file = temp.path() / "debug.log";
        const RunResult stub = run(
            {"config", "get", "--config-dir", temp.path().string(),
             "--vault-password-stdin", "--debug", "--log-file", log_file.string()},
            kPasswordLine);
        CHECK_EQ(stub.code, 6);
        CHECK(contains(stub.err, "debug: config get"));
        CHECK(std::filesystem::exists(log_file));

        const std::string raw = read_text(log_file);
        CHECK(!contains(raw, "config get"));
        CHECK(!contains(raw, "not implemented"));

        const RunResult shown = run(
            {"logs", "show", "--config-dir", temp.path().string(), "--vault-password-stdin",
             "--log-file", log_file.string()},
            kPasswordLine);
        CHECK_EQ(shown.code, 0);
        CHECK(contains(shown.out, "config get"));
    }

    return test_result();
}
