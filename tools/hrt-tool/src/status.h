#pragma once

#include "commands.h"
#include "local_commands.h"
#include "session.h"

#include <istream>
#include <nlohmann/json.hpp>
#include <ostream>
#include <string>

nlohmann::json build_status_view(const ParsedArgs &parsed,
                                 const nlohmann::json &payload, bool all,
                                 std::ostream &err);

void render_status_table(const nlohmann::json &view, bool all, std::ostream &out);

int parse_watch_seconds(const std::string &text); // > 0, throws Usage
