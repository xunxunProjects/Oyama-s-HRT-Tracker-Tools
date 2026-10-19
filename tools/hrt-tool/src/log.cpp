#include "log.h"

#include "hrtcore/hrtcore.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

constexpr size_t kNonceLen = 12;
constexpr size_t kTagLen = 16;
constexpr size_t kPrefixLen = 24;
constexpr size_t kMaxLineBytes = 64 * 1024;
constexpr size_t kMaxPayloadBytes = kMaxLineBytes + 8 + kNonceLen + kTagLen;

void put_u32(uint8_t *out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value >> 24);
    out[1] = static_cast<uint8_t>(value >> 16);
    out[2] = static_cast<uint8_t>(value >> 8);
    out[3] = static_cast<uint8_t>(value);
}

void put_u64(uint8_t *out, uint64_t value) {
    for (size_t i = 0; i < 8; ++i) {
        out[i] = static_cast<uint8_t>(value >> (56 - i * 8));
    }
}

uint32_t get_u32(const uint8_t *in) {
    return (static_cast<uint32_t>(in[0]) << 24) | (static_cast<uint32_t>(in[1]) << 16) |
           (static_cast<uint32_t>(in[2]) << 8) | static_cast<uint32_t>(in[3]);
}

uint64_t now_milliseconds() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

void append_bytes(const std::filesystem::path &path, const std::vector<uint8_t> &bytes) {
#ifdef _WIN32
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) {
        throw ToolError(ExitCode::Vault, "LOG_WRITE", "cannot open the log file");
    }
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        throw ToolError(ExitCode::Vault, "LOG_WRITE", "cannot append to the log file");
    }
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) {
        throw ToolError(ExitCode::Vault, "LOG_WRITE",
                        "cannot open the log file: " + std::string(std::strerror(errno)));
    }

    struct stat info {};
    if (::fstat(fd, &info) != 0 || (info.st_mode & 0077) != 0) {
        ::close(fd);
        throw ToolError(ExitCode::Vault, "LOG_PERMISSIONS",
                        "log file must not be group- or world-accessible");
    }

    std::string failure;
    size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t count = ::write(fd, bytes.data() + written, bytes.size() - written);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            failure = std::string(std::strerror(errno));
            break;
        }
        written += static_cast<size_t>(count);
    }
    if (failure.empty() && ::fsync(fd) != 0) {
        failure = std::string(std::strerror(errno));
    }
    ::close(fd);
    if (!failure.empty()) {
        throw ToolError(ExitCode::Vault, "LOG_WRITE", "cannot append to the log file: " + failure);
    }
#endif
}

} // namespace

void EncryptedLog::append(const std::filesystem::path &path,
                          const std::array<uint8_t, 32> &log_key, const std::string &line) {
    if (line.size() > kMaxLineBytes) {
        throw ToolLimitError("log line exceeds 64 KiB");
    }

    std::array<uint8_t, kNonceLen> nonce{};
    hrtcore_error error{};
    if (hrtcore_random_bytes(nonce.data(), nonce.size(), &error) != HRTCORE_OK) {
        throw ToolError(ExitCode::Vault, "LOG_CRYPTO", error.message);
    }

    const size_t ciphertext_len = line.size() + kTagLen;
    const uint32_t payload_len =
        static_cast<uint32_t>(8 + kNonceLen + ciphertext_len);

    uint8_t prefix[kPrefixLen] = {};
    put_u32(prefix, payload_len);
    put_u64(prefix + 4, now_milliseconds());
    std::memcpy(prefix + 12, nonce.data(), kNonceLen);

    std::vector<uint8_t> ciphertext(ciphertext_len);
    size_t written = 0;
    const int result = hrtcore_aes256gcm_encrypt(
        log_key.data(), log_key.size(), nonce.data(), nonce.size(), prefix, kPrefixLen,
        reinterpret_cast<const uint8_t *>(line.data()), line.size(), ciphertext.data(),
        ciphertext.size(), &written, &error);
    if (result != HRTCORE_OK) {
        throw ToolError(ExitCode::Vault, "LOG_CRYPTO", error.message);
    }
    if (written != ciphertext_len) {
        throw ToolError(ExitCode::Vault, "LOG_CRYPTO", "unexpected log ciphertext length");
    }

    std::vector<uint8_t> record(kPrefixLen + ciphertext_len);
    std::memcpy(record.data(), prefix, kPrefixLen);
    std::memcpy(record.data() + kPrefixLen, ciphertext.data(), ciphertext_len);
    append_bytes(path, record);
}

void EncryptedLog::show(const std::filesystem::path &path,
                        const std::array<uint8_t, 32> &log_key, std::ostream &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw ToolError(ExitCode::Vault, "LOG_READ", "cannot open the log file");
    }

    while (true) {
        uint8_t length_bytes[4] = {};
        in.read(reinterpret_cast<char *>(length_bytes), sizeof(length_bytes));
        if (in.gcount() == 0 && in.eof()) {
            break;
        }
        if (in.gcount() != static_cast<std::streamsize>(sizeof(length_bytes))) {
            throw LogCorruptError("truncated log record header");
        }

        const uint32_t payload_len = get_u32(length_bytes);
        if (payload_len < 8 + kNonceLen + kTagLen || payload_len > kMaxPayloadBytes) {
            throw LogCorruptError("log record length is out of range");
        }

        std::vector<uint8_t> payload(payload_len);
        in.read(reinterpret_cast<char *>(payload.data()),
                static_cast<std::streamsize>(payload_len));
        if (in.gcount() != static_cast<std::streamsize>(payload_len)) {
            throw LogCorruptError("truncated log record");
        }

        uint8_t aad[kPrefixLen] = {};
        std::memcpy(aad, length_bytes, sizeof(length_bytes));
        std::memcpy(aad + 4, payload.data(), 20);

        const uint8_t *nonce = payload.data() + 8;
        const uint8_t *ciphertext = payload.data() + 20;
        const size_t ciphertext_len = payload_len - 20;
        const size_t plaintext_cap = ciphertext_len - kTagLen;

        std::vector<uint8_t> plaintext(std::max<size_t>(plaintext_cap, 1));
        size_t plaintext_len = 0;
        hrtcore_error error{};
        const int result = hrtcore_aes256gcm_decrypt(
            log_key.data(), log_key.size(), nonce, kNonceLen, aad, kPrefixLen, ciphertext,
            ciphertext_len, plaintext.data(), plaintext_cap, &plaintext_len, &error);
        if (result != HRTCORE_OK) {
            hrtcore_secure_zero(plaintext.data(), plaintext.size());
            if (result == HRTCORE_ERR_AUTH) {
                throw LogAuthError("log record authentication failed");
            }
            throw ToolError(ExitCode::Vault, "LOG_CRYPTO", error.message);
        }
        plaintext.resize(plaintext_len);

        out.write(reinterpret_cast<const char *>(plaintext.data()),
                  static_cast<std::streamsize>(plaintext.size()));
        if (plaintext.empty() || plaintext.back() != '\n') {
            out.put('\n');
        }
        hrtcore_secure_zero(plaintext.data(), plaintext.size());
    }
}
