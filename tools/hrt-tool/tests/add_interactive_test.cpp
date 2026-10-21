#include "add_interactive.h"
#include "local_commands.h"
#include "test_framework.h"

#include <sstream>

int main() {
    CHECK_NEAR(parse_time_hours("2026,10,20:18:29", true),
               497922.4833333333, 1e-6); // 2026-10-20T18:29Z in hours

    AddDoseAnswers answers;
    std::istringstream in(
        "Estradiol valerate\n2mg\n2026,10,20:18:29\ninjection\nyes\n");
    std::ostringstream out;
    GlobalOptions options;
    CHECK(prompt_add_dose(in, out, options, &answers));
    CHECK_EQ(answers.drug, std::string("Estradiol valerate"));
    CHECK_EQ(answers.dose, std::string("2mg"));
    CHECK_EQ(answers.route, std::string("injection"));
    CHECK(out.str().find("[Yes/No]") != std::string::npos);
    return test_result();
}
