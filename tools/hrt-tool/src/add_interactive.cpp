#include "add_interactive.h"

#include "error.h"
#include "local_commands.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

namespace {

std::string trim(const std::string &text) {
    size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool read_line(std::istream &in, std::string *line) {
    if (!std::getline(in, *line)) {
        return false;
    }
    if (!line->empty() && line->back() == '\r') {
        line->pop_back();
    }
    return true;
}

bool prompt_value(std::istream &in, std::ostream &out, const std::string &label,
                  const std::string &previous, std::string *value) {
    out << label;
    if (!previous.empty()) {
        out << " [" << previous << "]";
    }
    out << "\n> ";
    out.flush();
    std::string line;
    if (!read_line(in, &line)) {
        return false;
    }
    line = trim(line);
    *value = line.empty() ? previous : line;
    return true;
}

} // namespace

bool prompt_add_dose(std::istream &in, std::ostream &out,
                     const GlobalOptions &options, AddDoseAnswers *answers) {
    AddDoseAnswers current;
    while (true) {
        std::string drug;
        if (!prompt_value(in, out, "Drug Categories:", current.drug, &drug)) {
            return false;
        }
        if (drug.empty()) {
            out << "drug is required\n";
            continue;
        }

        std::string dose;
        const std::string::size_type space = drug.rfind(' ');
        if (space != std::string::npos) {
            const std::string tail = drug.substr(space + 1);
            try {
                (void)parse_dose_mg(tail);
                dose = tail;
                drug = trim(drug.substr(0, space));
            } catch (const ToolError &) {
                /* Not an embedded dose; it stays part of the drug name. */
            }
        }
        try {
            (void)parse_ester(drug);
        } catch (const ToolError &error) {
            out << error.what() << "\n";
            continue;
        }

        if (dose.empty() &&
            !prompt_value(in, out, "Dosage:", current.dose, &dose)) {
            return false;
        }
        try {
            (void)parse_dose_mg(dose);
        } catch (const ToolError &error) {
            out << error.what() << "\n";
            continue;
        }

        std::string time;
        if (!prompt_value(in, out, "Timing of Medication:", current.time, &time)) {
            return false;
        }
        try {
            (void)parse_time_hours(time, options.utc);
        } catch (const ToolError &error) {
            out << error.what() << "\n";
            continue;
        }

        std::string route;
        if (!prompt_value(in, out, "How to take the medication:", current.route,
                          &route)) {
            return false;
        }
        if (!route.empty()) {
            try {
                (void)parse_route(route);
            } catch (const ToolError &error) {
                out << error.what() << "\n";
                continue;
            }
        }

        out << "Time of medication (" << time << ") Type of medication (" << drug
            << " " << dose;
        if (!route.empty()) {
            out << " " << route;
        }
        out << ")..........[Yes/No]: ";
        out.flush();
        std::string answer;
        if (!read_line(in, &answer)) {
            return false;
        }
        answer = trim(answer);
        std::transform(answer.begin(), answer.end(), answer.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        if (answer == "yes" || answer == "y") {
            answers->drug = drug;
            answers->dose = dose;
            answers->time = time;
            answers->route = route;
            return true;
        }
        if (answer == "no" || answer == "n") {
            current.drug = drug;
            current.dose = dose;
            current.time = time;
            current.route = route;
            continue;
        }
        out << "please answer yes or no\n";
    }
}

int handle_add_interactive(const ParsedArgs &parsed, std::istream &in,
                           std::ostream &out, std::ostream &err,
                           SessionContext *context) {
    AddDoseAnswers answers;
    if (!prompt_add_dose(in, out, parsed.options, &answers)) {
        out << "cancelled\n";
        return 0;
    }

    ParsedArgs add = parsed;
    add.command = "add";
    add.command_args = {"dose", "--drug", answers.drug, "--time", answers.time,
                        "--measurement", answers.dose};
    if (!answers.route.empty()) {
        add.command_args.push_back("--route");
        add.command_args.push_back(answers.route);
    }

    std::function<void(const std::string &)> previous_progress;
    if (context != nullptr) {
        previous_progress = context->progress;
        context->progress = [&out](const std::string &phase) {
            out << "  " << phase << "...\n";
            out.flush();
        };
    }

    out << "Recording...\n";
    out.flush();
    std::ostringstream add_out;
    int code = 0;
    try {
        code = dispatch(add, in, add_out, err, context);
    } catch (...) {
        if (context != nullptr) {
            context->progress = previous_progress;
        }
        throw;
    }
    if (context != nullptr) {
        context->progress = previous_progress;
    }
    out << add_out.str();
    if (code != 0) {
        return code;
    }

    const std::string user =
        context != nullptr && !context->session.data.username.empty()
            ? context->session.data.username
            : "unknown";
    out << "hrt:YES\n";
    out << "-------------------------------------\n";
    out << "- Timestamp: " << answers.time << "\n";
    out << "- User: " << user << "\n";
    out << "-------------------------------------\n";
    out << "........................................................... 100% done\n";
    return 0;
}
