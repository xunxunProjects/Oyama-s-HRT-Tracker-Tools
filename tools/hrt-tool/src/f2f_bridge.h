#pragma once

#include <ostream>
#include <string>
#include <vector>

int run_f2f(const std::vector<std::string> &args, std::ostream &out,
            std::ostream &err);
std::string f2f_version();
