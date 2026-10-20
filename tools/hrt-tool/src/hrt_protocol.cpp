#include "hrt_protocol.h"

#include "error.h"

#include <cstdint>
#include <string>

namespace {

constexpr size_t kMaxEnvelopeIdBytes = 128;

void validate_envelope(const nlohmann::json &message) {
    if (!message.is_object()) {
        throw ToolError(ExitCode::Data, "BAD_FRAME",
                        "hrt frame envelope must be a JSON object");
    }
    if (!message.contains("id") || !message.at("id").is_string()) {
        throw ToolError(ExitCode::Data, "BAD_FRAME",
                        "hrt frame envelope has no string id");
    }
    const std::string id = message.at("id").get<std::string>();
    if (id.empty() || id.size() > kMaxEnvelopeIdBytes) {
        throw ToolError(ExitCode::Data, "BAD_FRAME",
                        "hrt frame envelope id is empty or too long");
    }
}

} // namespace

std::string encode_hrt_frame(const nlohmann::json &message) {
    validate_envelope(message);
    const std::string payload = message.dump();
    if (payload.size() > kHrtMaxFrameBytes) {
        throw ToolError(ExitCode::Data, "FRAME_TOO_LARGE",
                        "hrt frame exceeds 24 MiB");
    }
    const uint32_t length = static_cast<uint32_t>(payload.size());
    std::string frame;
    frame.reserve(4 + payload.size());
    frame.push_back(static_cast<char>((length >> 24) & 0xff));
    frame.push_back(static_cast<char>((length >> 16) & 0xff));
    frame.push_back(static_cast<char>((length >> 8) & 0xff));
    frame.push_back(static_cast<char>(length & 0xff));
    frame += payload;
    return frame;
}

nlohmann::json decode_hrt_frame(const std::string &frame) {
    if (frame.size() < 4) {
        throw ToolError(ExitCode::Data, "BAD_FRAME",
                        "hrt frame has no length prefix");
    }
    const uint32_t length =
        (static_cast<uint32_t>(static_cast<unsigned char>(frame[0])) << 24) |
        (static_cast<uint32_t>(static_cast<unsigned char>(frame[1])) << 16) |
        (static_cast<uint32_t>(static_cast<unsigned char>(frame[2])) << 8) |
        static_cast<uint32_t>(static_cast<unsigned char>(frame[3]));
    if (length > kHrtMaxFrameBytes) {
        throw ToolError(ExitCode::Data, "FRAME_TOO_LARGE",
                        "hrt frame exceeds 24 MiB");
    }
    if (frame.size() - 4 != length) {
        throw ToolError(ExitCode::Data, "BAD_FRAME",
                        "hrt frame length does not match the payload");
    }
    nlohmann::json message = nlohmann::json::parse(
        frame.begin() + 4, frame.end(), nullptr, false);
    if (message.is_discarded()) {
        throw ToolError(ExitCode::Data, "BAD_FRAME",
                        "hrt frame payload is not valid JSON");
    }
    validate_envelope(message);
    return message;
}
