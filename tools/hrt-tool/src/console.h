#pragma once

#include "commands.h"

#include <istream>
#include <ostream>
#include <string>
#include <vector>

std::vector<std::string> tokenize_console_line(const std::string &line,
                                               std::string *error);

int run_console(const GlobalOptions &options, std::istream &in, std::ostream &out,
                std::ostream &err);
