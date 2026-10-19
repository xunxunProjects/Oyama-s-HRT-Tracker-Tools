#include "cli.h"
#include "test_framework.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

#ifndef HRT_TOOL_TESTDATA_DIR
#error "HRT_TOOL_TESTDATA_DIR must be defined"
#endif

#ifndef _WIN32
#include <sys/stat.h>
#endif

using nlohmann::json;

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
            path_ = root / ("hrt-tool-local-test-" + std::to_string(++g_temp_counter));
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

void write_text(const std::filesystem::path &path, const std::string &text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
#ifndef _WIN32
    ::chmod(path.c_str(), 0600);
#endif
}

std::string read_text(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<std::string> snapshot_args(const std::filesystem::path &snapshot,
                                       const std::string &user_id,
                                       const std::filesystem::path &account_file) {
    return {"--snapshot", snapshot.string(), "--user-id", user_id,
            "--account-password-file", account_file.string()};
}

bool contains_id(const json &records, const std::string &id) {
    for (const json &record : records) {
        if (record.value("id", std::string{}) == id) {
            return true;
        }
    }
    return false;
}

std::string newest_event_id(const json &records) {
    std::string id;
    double best = -1.0;
    for (const json &record : records) {
        const double time_h = record.value("timeH", 0.0);
        if (time_h > best || id.empty()) {
            best = time_h;
            id = record.value("id", std::string{});
        }
    }
    return id;
}

} // namespace

int main() {
    std::ifstream fixture_file(std::string(HRT_TOOL_TESTDATA_DIR) + "/pk-vectors.json");
    if (!fixture_file) {
        std::cerr << "cannot open pk-vectors.json\n";
        return 1;
    }
    json fixture;
    fixture_file >> fixture;
    const json &snapshot_fixture = fixture.at("snapshot");
    const std::string user_id = snapshot_fixture.at("userId").get<std::string>();
    const std::string account_password =
        snapshot_fixture.at("password").get<std::string>();

    TempDir temp;
    const std::filesystem::path snapshot = temp.path() / "snapshot.json";
    write_text(snapshot, snapshot_fixture.at("bundle").dump(2));
    const std::filesystem::path account_file = temp.path() / "account.txt";
    write_text(account_file, account_password + "\n");
    const std::filesystem::path vault_file = temp.path() / "vault.txt";
    write_text(vault_file, "correct horse battery staple\n");

    const std::vector<std::string> base =
        snapshot_args(snapshot, user_id, account_file);

    {
        std::vector<std::string> args{"look"};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
        CHECK(contains(result.out, "Recent labs:"));
        CHECK(contains(result.out, "uncalibrated baseline"));
    }

    {
        std::vector<std::string> args{"look", "--json"};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
        CHECK(contains(result.out, "\"compounds\""));
    }

    {
        std::vector<std::string> args{
            "add", "--drug", "Estradiol valerate", "--time", "now",
            "--measurement", "2mg"};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
        CHECK(contains(result.out, "added "));
        CHECK(contains(result.err, "assuming route: injection"));
        const std::string raw = read_text(snapshot);
        CHECK(!contains(raw, "Estradiol valerate"));
    }

    {
        std::vector<std::string> args{"look", "--json"};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
        const json view = json::parse(result.out, nullptr, false);
        CHECK(!view.is_discarded());
        bool found = false;
        for (const json &compound : view.at("compounds")) {
            if (compound.value("drug", std::string{}) == "Estradiol valerate") {
                found = true;
                CHECK(contains(compound.value("now", std::string{}), "pg/mL"));
            }
        }
        CHECK(found);
    }

    std::string event_id;
    {
        std::vector<std::string> args{"dose", "list", "--json"};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
        const json events = json::parse(result.out, nullptr, false);
        CHECK(!events.is_discarded());
        event_id = newest_event_id(events);
        CHECK(!event_id.empty());
    }

    {
        std::vector<std::string> args{"dose", "delete", "--id", event_id};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
        CHECK(contains(result.out, "deleted "));
    }

    {
        std::vector<std::string> args{"dose", "list", "--json"};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
        const json events = json::parse(result.out, nullptr, false);
        CHECK(!contains_id(events, event_id));
    }

    std::string lab_id;
    {
        std::vector<std::string> args{"lab", "add", "--time", "now", "--value", "200",
                                      "--unit", "pg/ml", "--assay", "lcms"};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
        CHECK(contains(result.out, "added "));
    }

    {
        std::vector<std::string> args{"lab", "list", "--json"};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
        const json labs = json::parse(result.out, nullptr, false);
        CHECK(!labs.is_discarded());
        lab_id = newest_event_id(labs);
        bool found = false;
        for (const json &lab : labs) {
            if (lab.value("concValue", 0.0) == 200.0) {
                found = true;
            }
        }
        CHECK(found);
    }

    {
        std::vector<std::string> args{"lab", "delete", "--id", lab_id};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
    }

    {
        std::vector<std::string> args{"weight", "set", "--kg", "75"};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
    }

    {
        std::vector<std::string> args{"weight", "get"};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
        CHECK(contains(result.out, "75"));
    }

    {
        std::vector<std::string> args{"export", "--out", "-"};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
        CHECK(contains(result.out, "\"modes\""));
    }

    const std::filesystem::path export_file = temp.path() / "export.hrte";
    {
        std::vector<std::string> args{"export", "--out", export_file.string(),
                                      "--vault-password-file", vault_file.string()};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
        CHECK(std::filesystem::exists(export_file));
        CHECK(!contains(read_text(export_file), "\"modes\""));
    }

    {
        std::vector<std::string> args{"import", "--in", export_file.string(),
                                      "--vault-password-file", vault_file.string()};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
        CHECK(contains(result.out, "merged"));
    }

    {
        std::vector<std::string> args{"import", "--replace", "--in", export_file.string(),
                                      "--vault-password-file", vault_file.string()};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult refused = run(args, "no\n");
        CHECK_EQ(refused.code, 2);
    }

    {
        std::vector<std::string> args{"import", "--replace", "--yes", "--in",
                                      export_file.string(), "--vault-password-file",
                                      vault_file.string()};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
        CHECK(contains(result.out, "replaced"));
    }

    {
        const RunResult result = run({"add", "--drug", "EV", "--time", "now",
                                      "--measurement", "2mg"});
        CHECK_EQ(result.code, 2);
        CHECK(contains(result.err, "--snapshot"));
    }

    {
        std::vector<std::string> args{"add", "--snapshot", snapshot.string(),
                                      "--account-password-file", account_file.string(),
                                      "--drug", "EV", "--time", "now", "--measurement",
                                      "2mg"};
        const RunResult result = run(args);
        CHECK_EQ(result.code, 2);
        CHECK(contains(result.err, "user-id"));
    }

    return test_result();
}
