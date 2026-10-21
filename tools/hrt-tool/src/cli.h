#pragma once

#include "commands.h"

#include <istream>
#include <ostream>

int run_cli(int argc, const char *const *argv, std::istream &in, std::ostream &out,
            std::ostream &err);

/* Parse argv without dispatching. Exposed for tests and the console. */
ParsedArgs parse_cli_args(int argc, const char *const *argv);

void print_help(std::ostream &out);
void print_command_help(const std::string &command, std::ostream &out);
