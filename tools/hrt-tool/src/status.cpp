#include "status.h"

#include "error.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

nlohmann::json build_status_view(const ParsedArgs &parsed,
                                 const nlohmann::json &payload, bool all,
                                 std::ostream &err) {
    const CommandFlags flags =
        parse_command_flags(parsed.command_args, {"--at", "--labs", "--watch"}, {});
    const auto offsets = parse_offsets(flags);

    ParsedArgs view_args = parsed;
    if (all && std::find(view_args.command_args.begin(), view_args.command_args.end(),
                         "--all") == view_args.command_args.end()) {
        view_args.command_args.push_back("--all");
    }

    nlohmann::json view = build_pk_view(view_args, payload, offsets, err);
    view["rows"] = nlohmann::json::array();
    if (view.contains("compounds") && view["compounds"].is_array()) {
        for (const nlohmann::json &compound : view["compounds"]) {
            for (const nlohmann::json &offset : compound["offsets"]) {
                view["rows"].push_back({
                    {"drug", compound.value("drug", std::string{})},
                    {"dose", compound.value("dose", std::string{})},
                    {"lastDose", compound.value("lastDoseStatus", std::string{})},
                    {"now", compound.value("now", std::string{})},
                    {"offset", offset.value("label", std::string{})},
                    {"expected", offset.value("value", std::string{})},
                });
            }
        }
    }

    if (view.contains("warnings") && view["warnings"].is_array()) {
        for (const nlohmann::json &warning : view["warnings"]) {
            if (warning.is_string()) {
                err << warning.get<std::string>() << "\n";
            }
        }
    }
    (void)all;
    return view;
}

void render_status_table(const nlohmann::json &view, bool all, std::ostream &out) {
    (void)all;
    out << std::left << std::setw(24) << "Current time" << std::setw(28)
        << "Drug-Categories" << std::setw(30) << "Timing-of-Medication"
        << std::setw(36) << "blood drug concentration" << "Expected\n";
    out << std::string(150, '-') << "\n";

    const std::string now = view.value("nowStatus", std::string{});
    if (view.contains("rows") && view["rows"].is_array()) {
        for (const nlohmann::json &row : view["rows"]) {
            const std::string drug =
                row.value("drug", std::string{}) + "\\" +
                row.value("dose", std::string{});
            out << std::left << std::setw(24) << now << std::setw(28) << drug
                << std::setw(30) << row.value("lastDose", std::string{})
                << std::setw(36) << row.value("now", std::string{})
                << row.value("offset", std::string{}) << " "
                << row.value("expected", std::string{}) << "\n";
        }
    }
    out << "done.\n---\n";
    out << "Three recent blood tests:\n";

    std::vector<std::string> methods;
    if (view.contains("labs") && view["labs"].is_array()) {
        for (const nlohmann::json &lab : view["labs"]) {
            const std::string name = lab.value("name", std::string{});
            out << name;
            out << (name == "Estradiol" ? " level: " : ": ");
            out << lab.value("statusValue", std::string{}) << "\n";
            const std::string method = lab.value("statusMethod", std::string{});
            if (!method.empty() &&
                std::find(methods.begin(), methods.end(), method) == methods.end()) {
                methods.push_back(method);
            }
        }
    }
    out << "Testing method:\n";
    if (methods.empty()) {
        out << "(unknown)\n";
    } else {
        for (const std::string &method : methods) {
            out << method << "\n";
        }
    }
    out << "done\n";
}

int parse_watch_seconds(const std::string &text) {
    int value = 0;
    try {
        size_t used = 0;
        value = std::stoi(text, &used);
        if (used != text.size()) {
            throw std::invalid_argument("trailing junk");
        }
    } catch (...) {
        throw ToolError(ExitCode::Usage, "WATCH",
                        "--watch needs a positive number of seconds");
    }
    if (value <= 0) {
        throw ToolError(ExitCode::Usage, "WATCH",
                        "--watch needs a positive number of seconds");
    }
    return value;
}
