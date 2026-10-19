#pragma once

#include <istream>
#include <ostream>
#include <string>

std::string read_password(std::istream &in, std::ostream &out, const std::string &prompt,
                          bool confirm, bool no_echo);
