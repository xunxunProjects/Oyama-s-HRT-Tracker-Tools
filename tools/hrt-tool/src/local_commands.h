#pragma once

#include "commands.h"

#include <istream>
#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <string>

bool is_local_command(const std::string &command);

int handle_local_command(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                         std::ostream &err);

/* Render `look` from an already decrypted sync payload (remote mode). */
int handle_look_payload(const ParsedArgs &parsed, const nlohmann::json &payload,
                        std::ostream &out);

/* Pure payload mutations shared by local-file and live modes. Each returns
 * the new payload, or nullopt for a read-only subcommand (dose/lab list,
 * weight get) that only prints. User-facing lines go to `out`/`err`. */
nlohmann::json apply_add(const ParsedArgs &parsed, nlohmann::json payload,
                         std::ostream &out, std::ostream &err);
std::optional<nlohmann::json> apply_dose(const ParsedArgs &parsed,
                                         nlohmann::json payload,
                                         std::ostream &out);
std::optional<nlohmann::json> apply_lab(const ParsedArgs &parsed,
                                        nlohmann::json payload,
                                        std::ostream &out);
std::optional<nlohmann::json> apply_weight(const ParsedArgs &parsed,
                                           nlohmann::json payload,
                                           std::ostream &out);
