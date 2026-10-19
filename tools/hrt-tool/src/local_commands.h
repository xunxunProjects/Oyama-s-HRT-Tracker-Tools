#pragma once

#include "commands.h"

#include <istream>
#include <ostream>
#include <string>

bool is_local_command(const std::string &command);

int handle_local_command(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                         std::ostream &err);
