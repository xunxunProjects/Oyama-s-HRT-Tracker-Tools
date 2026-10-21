#pragma once

#include "command_flags.h"
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

/* Build the shared PK view (compounds, offsets, labs) without rendering it. */
nlohmann::json build_pk_view(
    const ParsedArgs &parsed, const nlohmann::json &payload,
    const std::vector<std::pair<std::string, double>> &offsets, std::ostream &err);

std::vector<std::pair<std::string, double>> parse_offsets(const CommandFlags &flags);
double parse_time_hours(const std::string &text, bool utc);
int parse_ester(const std::string &drug);
int parse_route(const std::string &route);
double parse_dose_mg(const std::string &text);

/* `status` from a local snapshot (or the --snapshot file). */
int handle_status_local(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                        std::ostream &err);

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
