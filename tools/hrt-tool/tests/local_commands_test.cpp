#include "cli.h"
#include "test_framework.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>
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
        /* The snapshot's birthYear must reach the PK engine: an older member
           clears the same oral dose more slowly and shows a higher level. */
        const double now_h =
            static_cast<double>(std::time(nullptr)) / 3600.0;
        const int current_year =
            static_cast<int>(1970.0 + now_h / (365.2425 * 24.0));
        const auto write_plain = [&](const char *name, json birth) {
            const json empty_deletions = {{"events", json::object()},
                                          {"labResults", json::object()},
                                          {"doseTemplates", json::object()}};
            const json event = {{"id", "dose-age"},
                                {"route", "oral"},
                                {"ester", "E2"},
                                {"doseMG", 2},
                                {"timeH", now_h - 1.0},
                                {"extras", json::object()}};
            const json block = {{"events", json::array({event})},
                                {"labResults", json::array()},
                                {"doseTemplates", json::array()},
                                {"deletions", empty_deletions}};
            json payload = {{"meta", {{"version", 2}}},
                            {"mode", "transfem"},
                            {"weight", 70},
                            {"modes", {{"transfem", block},
                                       {"transmasc", block}}}};
            if (!birth.is_null()) {
                payload["birthYear"] = birth;
                payload["birthYearUpdatedAt"] = 1;
            }
            const std::filesystem::path path = temp.path() / name;
            write_text(path, payload.dump(2));
            return path;
        };
        const auto level = [&](const std::filesystem::path &path) {
            const RunResult result =
                run({"look", "--json", "--snapshot", path.string()});
            CHECK_EQ(result.code, 0);
            const json view = json::parse(result.out);
            return view.at("compounds").at(0).at("now").get<std::string>();
        };

        const std::string none = level(write_plain("plain-none.json", json()));
        const std::string young =
            level(write_plain("plain-young.json", current_year - 25));
        const std::string old =
            level(write_plain("plain-old.json", current_year - 70));
        CHECK(!none.empty());
        CHECK(young != old);
        /* Concentrations are printed as text; compare the parsed leading
           number rather than the unit suffix. */
        const auto leading = [](const std::string &text) {
            return std::strtod(text.c_str(), nullptr);
        };
        CHECK(leading(young) < leading(old));
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

    // C1: export refuses to overwrite an existing file.
    {
        const std::filesystem::path existing = temp.path() / "existing.hrte";
        write_text(existing, "keep me");
        std::vector<std::string> args{"export", "--out", existing.string(),
                                      "--vault-password-file", vault_file.string()};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 3);
        CHECK_EQ(read_text(existing), std::string("keep me"));
    }

    // C2: concurrent adds serialize and keep every record.
    {
        std::vector<std::string> list_args{"dose", "list", "--json"};
        list_args.insert(list_args.end(), base.begin(), base.end());
        const json before = json::parse(run(list_args).out, nullptr, false);
        CHECK(!before.is_discarded());
        const int before_count = static_cast<int>(before.size());

        std::atomic<int> failures{0};
        std::vector<std::thread> workers;
        for (int i = 0; i < 4; ++i) {
            workers.emplace_back([&]() {
                std::vector<std::string> args{
                    "add", "--drug", "EV", "--time", "now", "--measurement", "1mg"};
                args.insert(args.end(), base.begin(), base.end());
                if (run(args).code != 0) {
                    ++failures;
                }
            });
        }
        for (std::thread &worker : workers) {
            worker.join();
        }
        CHECK_EQ(failures.load(), 0);
        const json after = json::parse(run(list_args).out, nullptr, false);
        CHECK(!after.is_discarded());
        CHECK_EQ(static_cast<int>(after.size()), before_count + 4);
    }

    // C3: an explicit import restores a tombstoned record and clears the tombstone.
    {
        std::vector<std::string> list_args{"dose", "list", "--json"};
        list_args.insert(list_args.end(), base.begin(), base.end());
        const json events = json::parse(run(list_args).out, nullptr, false);
        CHECK(!events.is_discarded());
        CHECK(!events.empty());
        const json restored = events.at(0);
        const std::string restored_id = restored.value("id", std::string{});
        CHECK(!restored_id.empty());

        std::vector<std::string> del{"dose", "delete", "--id", restored_id};
        del.insert(del.end(), base.begin(), base.end());
        CHECK_EQ(run(del).code, 0);

        const std::filesystem::path import_file = temp.path() / "restore.json";
        json payload;
        payload["meta"] = {{"version", 2}};
        payload["mode"] = "transfem";
        payload["modes"] = {{"transfem",
                             {{"events", json::array({restored})},
                              {"labResults", json::array()},
                              {"doseTemplates", json::array()}}}};
        write_text(import_file, payload.dump(2));

        std::vector<std::string> args{"import", "--in", import_file.string()};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
        CHECK(contains(result.out, "added 1"));

        CHECK(contains_id(json::parse(run(list_args).out, nullptr, false), restored_id));

        std::vector<std::string> export_args{"export", "--out", "-"};
        export_args.insert(export_args.end(), base.begin(), base.end());
        const json exported = json::parse(run(export_args).out, nullptr, false);
        CHECK(!exported.is_discarded());
        const json &deletions =
            exported.at("modes").at("transfem").at("deletions").at("events");
        CHECK(!deletions.contains(restored_id));
    }

    // I2: look does not attribute the shared E2 curve to non-contributing rows.
    {
        std::vector<std::string> args{
            "add", "--drug", "spironolactone", "--time", "now", "--measurement", "50mg"};
        args.insert(args.end(), base.begin(), base.end());
        CHECK_EQ(run(args).code, 0);

        std::vector<std::string> remove_args{"add", "--drug", "EV", "--route",
                                             "patchRemove", "--time", "now",
                                             "--measurement", "1mg"};
        remove_args.insert(remove_args.end(), base.begin(), base.end());
        CHECK_EQ(run(remove_args).code, 0);

        std::vector<std::string> look_args{"look", "--json"};
        look_args.insert(look_args.end(), base.begin(), base.end());
        const RunResult result = run(look_args);
        CHECK_EQ(result.code, 0);
        const json view = json::parse(result.out, nullptr, false);
        CHECK(!view.is_discarded());
        for (const json &compound : view.at("compounds")) {
            CHECK(compound.value("drug", std::string{}) != "Unmodelled drug");
            CHECK(compound.value("route", std::string{}) != "patchRemove");
        }
    }

    // I4: lab values and units are validated strictly.
    {
        for (const char *value : {"nan", "2OO", "1e999", "-5"}) {
            std::vector<std::string> args{"lab", "add", "--time", "now", "--value",
                                          value, "--unit", "pg/ml"};
            args.insert(args.end(), base.begin(), base.end());
            const RunResult result = run(args);
            CHECK_EQ(result.code, 2);
            CHECK(contains(result.err, "value"));
        }

        std::vector<std::string> add{"lab", "add", "--time", "now", "--value",
                                     "250", "--unit", "pg/ml"};
        add.insert(add.end(), base.begin(), base.end());
        CHECK_EQ(run(add).code, 0);

        std::vector<std::string> list_args{"lab", "list", "--json"};
        list_args.insert(list_args.end(), base.begin(), base.end());
        const json labs = json::parse(run(list_args).out, nullptr, false);
        CHECK(!labs.is_discarded());
        std::string lab_id;
        for (const json &lab : labs) {
            if (lab.value("concValue", 0.0) == 250.0) {
                lab_id = lab.value("id", std::string{});
            }
        }
        CHECK(!lab_id.empty());

        std::vector<std::string> edit{"lab", "edit", "--id", lab_id, "--unit", "pmol/L"};
        edit.insert(edit.end(), base.begin(), base.end());
        CHECK_EQ(run(edit).code, 0);
        const json updated = json::parse(run(list_args).out, nullptr, false);
        bool unit_ok = false;
        for (const json &lab : updated) {
            if (lab.value("id", std::string{}) == lab_id) {
                unit_ok = lab.value("unit", std::string{}) == "pmol/l";
            }
        }
        CHECK(unit_ok);

        std::vector<std::string> bad_edit{"lab", "edit", "--id", lab_id, "--value", "nan"};
        bad_edit.insert(bad_edit.end(), base.begin(), base.end());
        CHECK_EQ(run(bad_edit).code, 2);
    }

    // I5: an explicit ISO 8601 offset selects the instant it names.
    {
        std::tm expected_tm{};
        expected_tm.tm_year = 2026 - 1900;
        expected_tm.tm_mon = 10 - 1;
        expected_tm.tm_mday = 19;
        expected_tm.tm_hour = 14;
        expected_tm.tm_min = 50;
        expected_tm.tm_sec = 30;
#ifdef _WIN32
        const double expected_h = static_cast<double>(_mkgmtime(&expected_tm)) / 3600.0;
#else
        const double expected_h = static_cast<double>(timegm(&expected_tm)) / 3600.0;
#endif

        std::vector<std::string> args{"add", "--drug", "EV", "--time",
                                      "2026-10-19T22:50:30+08:00", "--measurement",
                                      "2mg", "--utc"};
        args.insert(args.end(), base.begin(), base.end());
        CHECK_EQ(run(args).code, 0);

        std::vector<std::string> list_args{"dose", "list", "--json"};
        list_args.insert(list_args.end(), base.begin(), base.end());
        const json events = json::parse(run(list_args).out, nullptr, false);
        CHECK(!events.is_discarded());
        bool instant_ok = false;
        for (const json &event : events) {
            if (std::fabs(event.value("timeH", 0.0) - expected_h) < 0.001) {
                instant_ok = true;
            }
        }
        CHECK(instant_ok);

        std::vector<std::string> fraction{"add", "--drug", "EV", "--time",
                                          "2026-10-19T14:50:30.5Z", "--measurement",
                                          "2mg", "--utc"};
        fraction.insert(fraction.end(), base.begin(), base.end());
        CHECK_EQ(run(fraction).code, 0);
        const json with_fraction = json::parse(run(list_args).out, nullptr, false);
        CHECK(!with_fraction.is_discarded());
        bool fraction_ok = false;
        for (const json &event : with_fraction) {
            if (std::fabs(event.value("timeH", 0.0) - (expected_h + 0.5 / 3600.0)) <
                0.0001) {
                fraction_ok = true;
            }
        }
        CHECK(fraction_ok);

        std::vector<std::string> cleaned{
            "add", "--drug", "EV", "--time", "now", "--measurement",
            "12.499934062706513mg"};
        cleaned.insert(cleaned.end(), base.begin(), base.end());
        CHECK_EQ(run(cleaned).code, 0);
        const json cleaned_events = json::parse(run(list_args).out, nullptr, false);
        CHECK(!cleaned_events.is_discarded());
        bool cleaned_ok = false;
        for (const json &event : cleaned_events) {
            if (event.value("doseMG", 0.0) == 12.5) {
                cleaned_ok = true;
            }
        }
        CHECK(cleaned_ok);

        std::vector<std::string> junk{"add", "--drug", "EV", "--time",
                                      "2026-10-19T22:50:30Z junk", "--measurement",
                                      "2mg"};
        junk.insert(junk.end(), base.begin(), base.end());
        CHECK_EQ(run(junk).code, 2);
    }

    // I6: an oversized plaintext import fails with the typed 16 MiB limit.
    {
        const std::filesystem::path big = temp.path() / "big.json";
        write_text(big, std::string(16 * 1024 * 1024 + 1, 'x'));
        std::vector<std::string> args{"import", "--in", big.string()};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 5);
        CHECK(contains(result.err, "16 MiB"));
    }

#ifndef _WIN32
    // I6: a FIFO is rejected as a non-regular file instead of blocking.
    {
        const std::filesystem::path fifo = temp.path() / "snapshot.fifo";
        CHECK_EQ(::mkfifo(fifo.c_str(), 0600), 0);
        const RunResult result = run({"look", "--snapshot", fifo.string()});
        CHECK_EQ(result.code, 5);
        CHECK(contains(result.err, "regular file"));
    }
#endif

    // I7: naked-array v1 payloads are accepted as events.
    {
        const std::filesystem::path array_file = temp.path() / "array.json";
        json array = json::array();
        array.push_back({{"id", "naked-array-event"},
                         {"route", "oral"},
                         {"ester", "E2"},
                         {"doseMG", 2},
                         {"timeH", 100}});
        write_text(array_file, array.dump(2));

        std::vector<std::string> args{"import", "--in", array_file.string()};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
        CHECK(contains(result.out, "added 1"));

        const std::filesystem::path array_snapshot = temp.path() / "array-snapshot.json";
        write_text(array_snapshot, array.dump(2));
        const RunResult look = run({"look", "--snapshot", array_snapshot.string()});
        CHECK_EQ(look.code, 0);
    }

    // Malformed records produce a typed data error, not an untyped crash.
    {
        json bad_event = {{"id", "bad-record"}, {"route", "oral"},
                          {"ester", "E2"},     {"doseMG", "2"},
                          {"timeH", 100}};
        json bad;
        bad["meta"] = {{"version", 2}};
        bad["mode"] = "transfem";
        bad["modes"] = {{"transfem",
                         {{"events", json::array({bad_event})},
                          {"labResults", json::array()},
                          {"doseTemplates", json::array()}}}};
        const std::filesystem::path bad_file = temp.path() / "bad.json";
        write_text(bad_file, bad.dump(2));
        const RunResult result = run({"look", "--snapshot", bad_file.string()});
        CHECK_EQ(result.code, 5);
        CHECK(contains(result.err, "[DATA]") || contains(result.err, "[JSON]"));
    }

    // I3: imports are sanitized the way the website sanitizes them.
    {
        json dropped = {{"id", "sanitize-dropped"},
                        {"route", "bogus"},
                        {"ester", "E2"},
                        {"doseMG", 2},
                        {"timeH", 100}};
        json coerced = {{"id", "sanitize-coerced"},
                        {"route", "oral"},
                        {"ester", "E2"},
                        {"doseMG", "2"},
                        {"timeH", "100"}};
        json payload;
        payload["meta"] = {{"version", 2}};
        payload["mode"] = "transfem";
        payload["weight"] = 1.0e-300;
        payload["weightUpdatedAt"] = 1800000000000LL;
        payload["modes"] = {{"transfem",
                             {{"events", json::array({dropped, coerced})},
                              {"labResults", json::array()},
                              {"doseTemplates", json::array()}}}};
        const std::filesystem::path file = temp.path() / "sanitize.json";
        write_text(file, payload.dump(2));

        std::vector<std::string> args{"import", "--in", file.string()};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 0);
        CHECK(contains(result.out, "dropped 1"));

        std::vector<std::string> list_args{"dose", "list", "--json"};
        list_args.insert(list_args.end(), base.begin(), base.end());
        const json events = json::parse(run(list_args).out, nullptr, false);
        CHECK(!events.is_discarded());
        bool coerced_ok = false;
        for (const json &event : events) {
            const std::string id = event.value("id", std::string{});
            CHECK(id != "sanitize-dropped");
            if (id == "sanitize-coerced") {
                coerced_ok = event.at("doseMG").is_number() &&
                             event.at("doseMG").get<double>() == 2.0 &&
                             event.at("timeH").get<double>() == 100.0;
            }
        }
        CHECK(coerced_ok);

        std::vector<std::string> weight_args{"weight", "get"};
        weight_args.insert(weight_args.end(), base.begin(), base.end());
        const RunResult weight = run(weight_args);
        CHECK_EQ(weight.code, 0);
        CHECK(contains(weight.out, "20"));

        std::vector<std::string> look_args{"look", "--json"};
        look_args.insert(look_args.end(), base.begin(), base.end());
        CHECK_EQ(run(look_args).code, 0);
    }

    // I3: an oversized import array is refused with the typed limit.
    {
        json many = json::array();
        for (int i = 0; i < 20001; ++i) {
            many.push_back({{"id", "many-" + std::to_string(i)},
                            {"route", "oral"},
                            {"ester", "E2"},
                            {"doseMG", 1},
                            {"timeH", 100}});
        }
        const std::filesystem::path file = temp.path() / "many.json";
        write_text(file, many.dump());
        std::vector<std::string> args{"import", "--in", file.string()};
        args.insert(args.end(), base.begin(), base.end());
        const RunResult result = run(args);
        CHECK_EQ(result.code, 5);
        CHECK(contains(result.err, "too many"));
    }

    return test_result();
}
