#include "encrypted_file.h"

#include "entropy.h"
#include "hrtcore/hrtcore.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

constexpr uint16_t kVersion = 1;
constexpr uint16_t kKdfPbkdf2Sha256 = 1;
constexpr uint32_t kIterations = 600000;
constexpr uint32_t kMinIterations = 100000;
constexpr uint32_t kMaxIterations = 1000000;
constexpr size_t kSaltLen = 16;
constexpr size_t kNonceLen = 12;
constexpr size_t kTagLen = 16;
constexpr size_t kKeyLen = 32;
constexpr size_t kHeaderLen = 48;
constexpr size_t kMaxFileBytes = 16 * 1024 * 1024;

void put_u16(uint8_t *out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value >> 8);
    out[1] = static_cast<uint8_t>(value);
}

void put_u32(uint8_t *out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value >> 24);
    out[1] = static_cast<uint8_t>(value >> 16);
    out[2] = static_cast<uint8_t>(value >> 8);
    out[3] = static_cast<uint8_t>(value);
}

uint16_t get_u16(const uint8_t *in) {
    return static_cast<uint16_t>((static_cast<uint16_t>(in[0]) << 8) | in[1]);
}

uint32_t get_u32(const uint8_t *in) {
    return (static_cast<uint32_t>(in[0]) << 24) | (static_cast<uint32_t>(in[1]) << 16) |
           (static_cast<uint32_t>(in[2]) << 8) | static_cast<uint32_t>(in[3]);
}

std::array<uint8_t, kKeyLen> derive_key(const std::string &password, const uint8_t *salt,
                                        size_t salt_len) {
    std::array<uint8_t, kKeyLen> key{};
    hrtcore_error error{};
    if (hrtcore_pbkdf2_sha256(reinterpret_cast<const uint8_t *>(password.data()),
                              password.size(), salt, salt_len, kIterations, key.data(),
                              key.size(), &error) != HRTCORE_OK) {
        throw ToolError(ExitCode::Vault, "EXPORT_CRYPTO", error.message);
    }
    return key;
}

void check_password(const std::string &password) {
    if (password.empty()) {
        throw ToolError(ExitCode::Usage, "EXPORT_PASSWORD",
                        "an export password is required");
    }
    if (password.size() > kMaxMasterPasswordBytes) {
        throw ToolLimitError("export password exceeds 256 bytes");
    }
}

void check_permissions(const std::filesystem::path &path) {
#ifndef _WIN32
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0) {
        throw ToolError(ExitCode::Vault, "EXPORT_READ",
                        "cannot stat the export: " + std::string(std::strerror(errno)));
    }
    if ((info.st_mode & 0077) != 0) {
        throw ToolError(ExitCode::Vault, "EXPORT_PERMISSIONS",
                        "export must not be group- or world-accessible");
    }
#endif
}

void write_atomic(const std::filesystem::path &path, const std::vector<uint8_t> &bytes) {
    const std::filesystem::path dir =
        path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error && !std::filesystem::is_directory(dir)) {
        throw ToolError(ExitCode::Vault, "EXPORT_WRITE",
                        "cannot create the export directory: " + error.message());
    }

#ifdef _WIN32
    const std::filesystem::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw ToolError(ExitCode::Vault, "EXPORT_WRITE",
                            "cannot create a temporary export");
        }
        out.write(reinterpret_cast<const char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            throw ToolError(ExitCode::Vault, "EXPORT_WRITE",
                            "cannot write the temporary export");
        }
    }
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw ToolError(ExitCode::Vault, "EXPORT_WRITE",
                        "cannot publish the export: " + error.message());
    }
#else
    static std::atomic<unsigned> counter{0};
    const std::filesystem::path temp =
        dir / (path.filename().string() + ".tmp." + std::to_string(::getpid()) + "." +
               std::to_string(counter.fetch_add(1)));
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        throw ToolError(ExitCode::Vault, "EXPORT_WRITE",
                        "cannot create a temporary export: " +
                            std::string(std::strerror(errno)));
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
    if (::close(fd) != 0 && failure.empty()) {
        failure = std::string(std::strerror(errno));
    }
    if (failure.empty() && ::rename(temp.c_str(), path.c_str()) != 0) {
        failure = std::string(std::strerror(errno));
    }
    if (!failure.empty()) {
        ::unlink(temp.c_str());
        throw ToolError(ExitCode::Vault, "EXPORT_WRITE",
                        "cannot write the export: " + failure);
    }
    const int dir_fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }
#endif
}

} // namespace

void seal_encrypted_file(const std::filesystem::path &path, const std::string &magic,
                         const std::string &plaintext, const std::string &password) {
    if (magic.size() != 4) {
        throw ToolError(ExitCode::Data, "EXPORT_MAGIC", "export magic must be 4 bytes");
    }
    check_password(password);
    if (plaintext.size() > kMaxFileBytes - kHeaderLen - kTagLen) {
        throw ToolLimitError("export payload is too large");
    }

    std::array<uint8_t, kSaltLen> salt{};
    std::array<uint8_t, kNonceLen> nonce{};
    hrtcore_error error{};
    if (hrtcore_random_bytes(salt.data(), salt.size(), &error) != HRTCORE_OK ||
        hrtcore_random_bytes(nonce.data(), nonce.size(), &error) != HRTCORE_OK) {
        throw ToolError(ExitCode::Vault, "EXPORT_CRYPTO", error.message);
    }

    const std::array<uint8_t, kKeyLen> key =
        derive_key(password, salt.data(), salt.size());

    uint8_t header[kHeaderLen] = {};
    std::memcpy(header, magic.data(), 4);
    put_u16(header + 4, kVersion);
    put_u16(header + 6, kKdfPbkdf2Sha256);
    put_u32(header + 8, kIterations);
    put_u16(header + 12, static_cast<uint16_t>(kSaltLen));
    std::memcpy(header + 14, salt.data(), kSaltLen);
    put_u16(header + 30, static_cast<uint16_t>(kNonceLen));
    std::memcpy(header + 32, nonce.data(), kNonceLen);
    const size_t ciphertext_len = plaintext.size() + kTagLen;
    put_u32(header + 44, static_cast<uint32_t>(ciphertext_len));

    std::vector<uint8_t> bytes(kHeaderLen + ciphertext_len);
    std::memcpy(bytes.data(), header, kHeaderLen);
    size_t written = 0;
    if (hrtcore_aes256gcm_encrypt(
            key.data(), key.size(), nonce.data(), nonce.size(), header, kHeaderLen,
            reinterpret_cast<const uint8_t *>(plaintext.data()), plaintext.size(),
            bytes.data() + kHeaderLen, ciphertext_len, &written, &error) != HRTCORE_OK) {
        throw ToolError(ExitCode::Vault, "EXPORT_CRYPTO", error.message);
    }
    if (written != ciphertext_len) {
        throw ToolError(ExitCode::Vault, "EXPORT_CRYPTO",
                        "unexpected export ciphertext length");
    }
    write_atomic(path, bytes);
}

std::string open_encrypted_file(const std::filesystem::path &path,
                                const std::string &magic, const std::string &password) {
    if (magic.size() != 4) {
        throw ToolError(ExitCode::Data, "EXPORT_MAGIC", "export magic must be 4 bytes");
    }
    check_password(password);
    check_permissions(path);

    std::error_code size_error;
    const auto size = std::filesystem::file_size(path, size_error);
    if (size_error) {
        throw ToolError(ExitCode::Vault, "EXPORT_READ",
                        "cannot read the export: " + size_error.message());
    }
    if (size > kMaxFileBytes) {
        throw ToolLimitError("export exceeds the 16 MiB limit");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw ToolError(ExitCode::Vault, "EXPORT_READ", "cannot open the export");
    }
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>()};
    if (bytes.size() < kHeaderLen) {
        throw ExportCorruptError("export header is truncated");
    }
    if (std::memcmp(bytes.data(), magic.data(), 4) != 0) {
        throw ExportCorruptError("export magic does not match");
    }
    const uint16_t version = get_u16(bytes.data() + 4);
    const uint16_t kdf = get_u16(bytes.data() + 6);
    const uint32_t iterations = get_u32(bytes.data() + 8);
    const uint16_t salt_len = get_u16(bytes.data() + 12);
    const uint16_t nonce_len = get_u16(bytes.data() + 30);
    const uint32_t ciphertext_len = get_u32(bytes.data() + 44);
    if (version != kVersion) {
        throw ExportCorruptError("unsupported export version");
    }
    if (kdf != kKdfPbkdf2Sha256) {
        throw ExportCorruptError("unsupported export KDF");
    }
    if (iterations < kMinIterations || iterations > kMaxIterations) {
        throw ExportCorruptError("export KDF iteration count is out of range");
    }
    if (salt_len != kSaltLen || nonce_len != kNonceLen) {
        throw ExportCorruptError("export salt or nonce length is invalid");
    }
    if (ciphertext_len < kTagLen || bytes.size() != kHeaderLen + ciphertext_len) {
        throw ExportCorruptError("export ciphertext length does not match the file");
    }

    std::array<uint8_t, kKeyLen> key{};
    hrtcore_error error{};
    if (hrtcore_pbkdf2_sha256(reinterpret_cast<const uint8_t *>(password.data()),
                              password.size(), bytes.data() + 14, salt_len, iterations,
                              key.data(), key.size(), &error) != HRTCORE_OK) {
        throw ToolError(ExitCode::Vault, "EXPORT_CRYPTO", error.message);
    }

    const size_t plaintext_cap = ciphertext_len - kTagLen;
    std::vector<uint8_t> plaintext(std::max<size_t>(plaintext_cap, 1));
    size_t written = 0;
    const int result = hrtcore_aes256gcm_decrypt(
        key.data(), key.size(), bytes.data() + 32, nonce_len, bytes.data(), kHeaderLen,
        bytes.data() + kHeaderLen, ciphertext_len, plaintext.data(), plaintext_cap,
        &written, &error);
    if (result == HRTCORE_ERR_AUTH) {
        hrtcore_secure_zero(plaintext.data(), plaintext.size());
        throw ExportAuthError("export password is wrong or the export was tampered with");
    }
    if (result != HRTCORE_OK) {
        hrtcore_secure_zero(plaintext.data(), plaintext.size());
        throw ToolError(ExitCode::Vault, "EXPORT_CRYPTO", error.message);
    }
    plaintext.resize(written);
    const std::string output(reinterpret_cast<const char *>(plaintext.data()),
                             plaintext.size());
    hrtcore_secure_zero(plaintext.data(), plaintext.size());
    return output;
}
