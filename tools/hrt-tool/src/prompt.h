#pragma once

#include <cstddef>
#include <istream>
#include <ostream>
#include <string>

std::string read_password_line(std::istream &in, size_t max_bytes);

std::string read_password(std::istream &in, std::ostream &out, const std::string &prompt,
                          bool confirm, bool no_echo);
