#pragma once

#include "commands.h"

#include <istream>
#include <nlohmann/json.hpp>
#include <ostream>
#include <string>

bool is_local_command(const std::string &command);

int handle_local_command(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                         std::ostream &err);

/* Render `look` from an already decrypted sync payload (remote mode). */
int handle_look_payload(const ParsedArgs &parsed, const nlohmann::json &payload,
                        std::ostream &out);
