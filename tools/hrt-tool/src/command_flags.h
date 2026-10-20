#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

struct CommandFlags {
    std::map<std::string, std::string> values;
    std::set<std::string> flags;
    std::vector<std::string> positionals;
    std::vector<std::string> extras;
};

CommandFlags parse_command_flags(const std::vector<std::string> &args,
                                 const std::set<std::string> &value_flags,
                                 const std::set<std::string> &boolean_flags);
