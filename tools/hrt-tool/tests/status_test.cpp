#include "cli.h"
#include "format.h"
#include "status.h"
#include "test_framework.h"

#include <filesystem>
#include <fstream>
#include <sstream>

int main() {
    CHECK_EQ(format_elapsed(24.0 + 11.0 + 10.0 / 60.0),
             std::string("1d , 11h , 10min"));
    CHECK_EQ(format_time_status(0.0, true),
             std::string("UTC +0 1970-01-01 00:00"));
    CHECK_EQ(format_time(0.0, true), std::string("1970-01-01 00:00 UTC"));
    CHECK_EQ(format_lab_value_status(169.0, "pg/ml"),
             std::string("169.0 pg/mL \\ 620.4 pmol/L"));
    CHECK_EQ(format_assay("lcms"), std::string("Mass spectrometry"));
    CHECK_EQ(parse_watch_seconds("5"), 5);
    CHECK_THROWS(parse_watch_seconds("0"), ToolError);
    CHECK_THROWS(parse_watch_seconds("abc"), ToolError);

    nlohmann::json payload = {
        {"modes",
         {{"transfem",
           {{"events", nlohmann::json::array()},
            {"labResults", nlohmann::json::array()}}}}},
        {"weight", 70.0},
    };
    const double now = 20000.0;
    payload["modes"]["transfem"]["events"].push_back({
        {"id", "e1"},
        {"route", "injection"},
        {"ester", "EV"},
        {"doseMG", 2.0},
        {"timeH", now - 24.0},
        {"extras", nlohmann::json::object()},
    });
    payload["modes"]["transfem"]["labResults"].push_back({
        {"id", "l1"},
        {"timeH", now - 1.0},
        {"concValue", 169.0},
        {"unit", "pg/ml"},
        {"assay", "lcms"},
    });
    ParsedArgs compact_args;
    compact_args.options.utc = true;
    const nlohmann::json compact =
        build_status_view(compact_args, payload, false, std::cout);
    CHECK_EQ(compact["rows"].size(), size_t{4});

    ParsedArgs parsed;
    parsed.options.utc = true;
    parsed.command_args = {"all"};
    nlohmann::json view = build_status_view(parsed, payload, true, std::cout);
    CHECK(view["rows"].is_array());
    CHECK(view["rows"].size() > 1);
    CHECK(view["rows"][0]["drug"] == "Estradiol valerate");
    std::ostringstream out;
    render_status_table(view, true, out);
    CHECK(out.str().find("Estradiol valerate\\2mg") != std::string::npos);
    CHECK(out.str().find("Expected") != std::string::npos);
    CHECK(out.str().find("Mass spectrometry") != std::string::npos);

    const std::filesystem::path snapshot =
        std::filesystem::temp_directory_path() / "hrt-status-snapshot.json";
    {
        std::ofstream file(snapshot);
        file << payload.dump();
    }
    const char *argv[] = {"hrt-tool", "status", "--snapshot",
                          snapshot.c_str(), "--utc"};
    std::istringstream command_in;
    std::ostringstream command_out;
    std::ostringstream command_err;
    CHECK_EQ(run_cli(5, argv, command_in, command_out, command_err), 0);
    CHECK(command_out.str().find("Expected") != std::string::npos);
    CHECK(command_out.str().find("Mass spectrometry") != std::string::npos);

    const char *argv_at[] = {"hrt-tool", "status", "--snapshot",
                             snapshot.c_str(), "--at", "1h,2h"};
    std::istringstream at_in;
    std::ostringstream at_out;
    std::ostringstream at_err;
    CHECK_EQ(run_cli(6, argv_at, at_in, at_out, at_err), 0);
    CHECK(at_out.str().find("1h") != std::string::npos);
    CHECK(at_out.str().find("2h") != std::string::npos);
    CHECK(at_out.str().find("30m") == std::string::npos);

    const char *argv_labs[] = {"hrt-tool", "status", "--snapshot",
                               snapshot.c_str(), "--labs", "1"};
    std::istringstream labs_in;
    std::ostringstream labs_out;
    std::ostringstream labs_err;
    CHECK_EQ(run_cli(6, argv_labs, labs_in, labs_out, labs_err), 0);

    const char *argv_watch[] = {"hrt-tool", "status", "--snapshot",
                                snapshot.c_str(), "--watch", "0"};
    std::istringstream watch_in;
    std::ostringstream watch_out;
    std::ostringstream watch_err;
    CHECK_EQ(run_cli(6, argv_watch, watch_in, watch_out, watch_err), 2);
    CHECK(watch_err.str().find("WATCH") != std::string::npos);

    const char *status_help[] = {"hrt-tool", "status", "--help"};
    std::istringstream help_in;
    std::ostringstream help_out;
    std::ostringstream help_err;
    CHECK_EQ(run_cli(3, status_help, help_in, help_out, help_err), 0);
    CHECK(help_out.str().find("--at") != std::string::npos);

    const char *help_status[] = {"hrt-tool", "help", "status"};
    std::istringstream help_in2;
    std::ostringstream help_out2;
    std::ostringstream help_err2;
    CHECK_EQ(run_cli(3, help_status, help_in2, help_out2, help_err2), 0);
    CHECK(help_out2.str().find("--at") != std::string::npos);

    std::filesystem::remove(snapshot);
    return test_result();
}
