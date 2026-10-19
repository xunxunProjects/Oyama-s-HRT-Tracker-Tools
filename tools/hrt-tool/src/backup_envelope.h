#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct EnvelopeImport {
    nlohmann::json sync_payload;
    std::vector<std::string> not_carried;
};

nlohmann::json export_complete_envelope(const nlohmann::json &sync_payload,
                                        const std::string &created_at);

EnvelopeImport import_complete_envelope(const nlohmann::json &envelope);
