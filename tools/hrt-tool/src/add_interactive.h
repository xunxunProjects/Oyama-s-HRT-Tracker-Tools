#pragma once

#include "commands.h"
#include "session.h"

#include <istream>
#include <ostream>
#include <string>

struct AddDoseAnswers {
    std::string drug;
    std::string dose;
    std::string time;
    std::string route;
};

bool prompt_add_dose(std::istream &in, std::ostream &out,
                     const GlobalOptions &options, AddDoseAnswers *answers);

int handle_add_interactive(const ParsedArgs &parsed, std::istream &in,
                           std::ostream &out, std::ostream &err,
                           SessionContext *context);
