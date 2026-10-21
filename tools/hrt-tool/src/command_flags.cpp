#include "command_flags.h"

#include "error.h"

CommandFlags parse_command_flags(const std::vector<std::string> &args,
                                 const std::set<std::string> &value_flags,
                                 const std::set<std::string> &boolean_flags) {
    CommandFlags out;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string token = args[i];
        if (token.empty() || token.front() != '-') {
            out.positionals.push_back(token);
            continue;
        }
        std::string name = token;
        std::string value;
        bool has_value = false;
        const std::string::size_type equals = token.find('=');
        if (equals != std::string::npos) {
            name = token.substr(0, equals);
            value = token.substr(equals + 1);
            has_value = true;
        }
        if (boolean_flags.count(name) != 0) {
            out.flags.insert(name);
            continue;
        }
        if (value_flags.count(name) == 0) {
            throw ToolError(ExitCode::Usage, "FLAG", "unknown flag: " + name);
        }
        if (!has_value) {
            if (i + 1 >= args.size()) {
                throw ToolError(ExitCode::Usage, "FLAG", "missing value for " + name);
            }
            value = args[++i];
        }
        if (name == "--extras") {
            out.extras.push_back(value);
        } else {
            out.values[name] = value;
        }
    }
    return out;
}
