#include "error.h"
#include "hrt_protocol.h"
#include "test_framework.h"

#include <nlohmann/json.hpp>
#include <string>

using nlohmann::json;

namespace {

uint32_t read_u32(const std::string &frame) {
    return (static_cast<uint32_t>(static_cast<unsigned char>(frame[0])) << 24) |
           (static_cast<uint32_t>(static_cast<unsigned char>(frame[1])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(frame[2])) << 8) |
           static_cast<uint32_t>(static_cast<unsigned char>(frame[3]));
}

} // namespace

int main() {
    {
        const json message{{"id", "c-1"}, {"op", "ping"}};
        const std::string frame = encode_hrt_frame(message);
        CHECK(frame.size() > 4);
        CHECK_EQ(read_u32(frame), static_cast<uint32_t>(frame.size() - 4));
        const json decoded = decode_hrt_frame(frame);
        CHECK_EQ(decoded, message);
    }

    {
        const json response{{"id", "c-1"}, {"status", 200},
                            {"body_base64", ""}};
        CHECK_EQ(decode_hrt_frame(encode_hrt_frame(response)), response);
    }

    {
        const json failure{{"id", "c-2"},
                           {"error", {{"code", "BAD_FRAME"},
                                      {"message", "no"}}}};
        CHECK_EQ(decode_hrt_frame(encode_hrt_frame(failure)), failure);
    }

    {
        CHECK_THROWS(encode_hrt_frame(json::array()), ToolError);
        CHECK_THROWS(encode_hrt_frame(json{{"op", "ping"}}), ToolError);
        CHECK_THROWS(encode_hrt_frame(json{{"id", 1}}), ToolError);
    }

    {
        const std::string big(kHrtMaxFrameBytes + 1, 'x');
        CHECK_THROWS(encode_hrt_frame(json{{"id", "big"}, {"body", big}}),
                     ToolError);
    }

    {
        /* Truncated length prefix. */
        CHECK_THROWS(decode_hrt_frame(std::string("\x00\x00", 2)), ToolError);
        /* Declared length larger than the buffer. */
        std::string truncated("\x00\x00\x00\x10", 4);
        truncated += "{}";
        CHECK_THROWS(decode_hrt_frame(truncated), ToolError);
        /* Declared length larger than the cap. */
        const std::string oversized("\xFF\xFF\xFF\xFF", 4);
        CHECK_THROWS(decode_hrt_frame(oversized), ToolError);
        /* Trailing bytes after the declared frame. */
        const std::string frame = encode_hrt_frame(json{{"id", "c-1"}});
        CHECK_THROWS(decode_hrt_frame(frame + "x"), ToolError);
        /* Invalid JSON inside a valid frame. */
        const std::string invalid_payload = "not json";
        std::string invalid("\x00\x00\x00\x08", 4);
        invalid += invalid_payload;
        CHECK_THROWS(decode_hrt_frame(invalid), ToolError);
        /* A JSON array is not an envelope. */
        const std::string array_payload = "[]";
        std::string array_frame("\x00\x00\x00\x02", 4);
        array_frame += array_payload;
        CHECK_THROWS(decode_hrt_frame(array_frame), ToolError);
    }

    return test_result();
}
