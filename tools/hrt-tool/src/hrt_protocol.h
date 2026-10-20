#pragma once

#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>

/* hrt/1 frame: u32 big-endian payload length, then one JSON object. */
constexpr size_t kHrtMaxFrameBytes = 1024 * 1024;

/* Serializes and length-prefixes one envelope. The envelope must be an
   object with a non-empty string id; op/status/error semantics are checked
   by the client or server that owns the connection. */
std::string encode_hrt_frame(const nlohmann::json &message);

/* Parses exactly one complete frame. Trailing bytes, a mismatched length,
   an oversized payload, or a malformed envelope are rejected. */
nlohmann::json decode_hrt_frame(const std::string &frame);
