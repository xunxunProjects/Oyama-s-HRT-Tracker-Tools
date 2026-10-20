#pragma once

#include "hrt_client.h"

#include <cstdint>
#include <string>

struct HrtPairRequest {
    std::string request_id;   /* 32 lowercase hex */
    std::string poll_secret;  /* 64 lowercase hex, never displayed */
    int64_t expires_at = 0;   /* Unix seconds */
    std::string verification_url;
};

enum class HrtPairStatus { Pending, Approved, Denied, Expired };

struct HrtPairResult {
    HrtPairStatus status = HrtPairStatus::Pending;
    std::string token;
    std::string user_id;
    std::string username;
    std::string device_fingerprint;
    std::string device_name;
};

/* Exchange the locally held pairing code for a server-side request. Only the
   code's SHA-256 leaves the machine. Throws ToolError on an invalid request
   or a non-success response. */
HrtPairRequest start_hrt_pairing(HrtClient &client, const std::string &username,
                                 const std::string &device_name,
                                 const std::string &device_certificate_pem,
                                 const std::string &code,
                                 const std::string &client_version);

/* One status poll. A held or unknown request is reported as Pending, exactly
   as the server does, so the CLI cannot learn that an account is in a hold. */
HrtPairResult poll_hrt_pairing(HrtClient &client, const HrtPairRequest &request);
