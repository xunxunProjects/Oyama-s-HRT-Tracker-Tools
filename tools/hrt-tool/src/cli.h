#pragma once

#include <istream>
#include <ostream>

int run_cli(int argc, const char *const *argv, std::istream &in, std::ostream &out,
            std::ostream &err);
