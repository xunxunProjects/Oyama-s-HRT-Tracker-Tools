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
    std::filesystem::remove(snapshot);
    return test_result();
}
