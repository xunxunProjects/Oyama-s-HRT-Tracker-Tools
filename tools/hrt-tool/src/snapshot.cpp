#include "snapshot.h"

#include "entropy.h"
#include "hrtcore/hrtcore.h"

#include <algorithm>
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

constexpr size_t kMaxSnapshotBytes = 16 * 1024 * 1024;
constexpr size_t kIvLen = 12;
constexpr size_t kTagLen = 16;
constexpr size_t kKeyLen = 32;
constexpr uint32_t kIterations = 600000;
constexpr int kCloudVersion = 1;

std::string b64_encode(const uint8_t *data, size_t length) {
    std::vector<char> out(4 * ((length + 2) / 3) + 1);
    size_t written = 0;
    hrtcore_error error{};
    if (hrtcore_b64_encode(data, length, out.data(), out.size(), &written, &error) !=
        HRTCORE_OK) {
        throw ToolError(ExitCode::Vault, "SNAPSHOT_BASE64", error.message);
    }
    return std::string(out.data(), written);
}

std::vector<uint8_t> b64_decode(const std::string &text) {
    std::vector<uint8_t> out((text.size() / 4) * 3 + 3);
    size_t written = 0;
    hrtcore_error error{};
    if (hrtcore_b64_decode(text.data(), text.size(), out.data(), out.size(), &written,
                           &error) != HRTCORE_OK) {
        throw SnapshotCorruptError(std::string("invalid base64 in snapshot: ") +
                                   error.message);
    }
    out.resize(written);
    return out;
}

void write_atomic(const std::filesystem::path &path, const std::string &text) {
    const std::filesystem::path dir =
        path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error && !std::filesystem::is_directory(dir)) {
        throw ToolError(ExitCode::Vault, "SNAPSHOT_WRITE",
                        "cannot create the snapshot directory: " + error.message());
    }

#ifdef _WIN32
    const std::filesystem::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw ToolError(ExitCode::Vault, "SNAPSHOT_WRITE",
                            "cannot create a temporary snapshot");
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            throw ToolError(ExitCode::Vault, "SNAPSHOT_WRITE",
                            "cannot write the temporary snapshot");
        }
    }
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw ToolError(ExitCode::Vault, "SNAPSHOT_WRITE",
                        "cannot publish the snapshot: " + error.message());
    }
#else
    static std::atomic<unsigned> counter{0};
    const std::filesystem::path temp =
        dir / (path.filename().string() + ".tmp." + std::to_string(::getpid()) + "." +
               std::to_string(counter.fetch_add(1)));
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        throw ToolError(ExitCode::Vault, "SNAPSHOT_WRITE",
                        "cannot create a temporary snapshot: " +
                            std::string(std::strerror(errno)));
    }
    std::string failure;
    size_t written = 0;
    while (written < text.size()) {
        const ssize_t count = ::write(fd, text.data() + written, text.size() - written);
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
        throw ToolError(ExitCode::Vault, "SNAPSHOT_WRITE",
                        "cannot write the snapshot: " + failure);
    }
    const int dir_fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }
#endif
}

} // namespace

std::array<uint8_t, 32> derive_cloud_key(const std::string &password,
                                         const std::string &user_id) {
    if (password.empty() || user_id.empty()) {
        throw ToolError(ExitCode::Usage, "SNAPSHOT_CREDENTIALS",
                        "an account password and user id are required");
    }
    if (password.size() > kMaxMasterPasswordBytes || user_id.size() > 256) {
        throw ToolLimitError("snapshot credentials are too long");
    }
    const std::string salt = "hrt-cloud-v1:" + user_id;
    std::array<uint8_t, kKeyLen> key{};
    hrtcore_error error{};
    if (hrtcore_pbkdf2_sha256(reinterpret_cast<const uint8_t *>(password.data()),
                              password.size(),
                              reinterpret_cast<const uint8_t *>(salt.data()), salt.size(),
                              kIterations, key.data(), key.size(), &error) != HRTCORE_OK) {
        throw ToolError(ExitCode::Vault, "SNAPSHOT_KEY", error.message);
    }
    return key;
}

bool is_cloud_bundle(const nlohmann::json &value) {
    return value.is_object() && value.contains("cloud") &&
           value.at("cloud").is_number() && value.at("cloud").get<int>() == kCloudVersion &&
           value.contains("iv") && value.at("iv").is_string() && value.contains("data") &&
           value.at("data").is_string();
}

nlohmann::json decrypt_cloud_bundle(const nlohmann::json &bundle,
                                    const std::array<uint8_t, 32> &key) {
    if (!is_cloud_bundle(bundle)) {
        throw SnapshotCorruptError("snapshot is not a cloud bundle");
    }
    const std::vector<uint8_t> iv = b64_decode(bundle.at("iv").get<std::string>());
    const std::vector<uint8_t> data = b64_decode(bundle.at("data").get<std::string>());
    if (iv.size() != kIvLen || data.size() < kTagLen) {
        throw SnapshotCorruptError("snapshot IV or ciphertext length is invalid");
    }

    const size_t plaintext_cap = data.size() - kTagLen;
    std::vector<uint8_t> plaintext(std::max<size_t>(plaintext_cap, 1));
    size_t written = 0;
    hrtcore_error error{};
    const int result = hrtcore_aes256gcm_decrypt(
        key.data(), key.size(), iv.data(), iv.size(), nullptr, 0, data.data(), data.size(),
        plaintext.data(), plaintext_cap, &written, &error);
    if (result == HRTCORE_ERR_AUTH) {
        hrtcore_secure_zero(plaintext.data(), plaintext.size());
        throw SnapshotAuthError("snapshot password is wrong or the snapshot was tampered with");
    }
    if (result != HRTCORE_OK) {
        hrtcore_secure_zero(plaintext.data(), plaintext.size());
        throw ToolError(ExitCode::Vault, "SNAPSHOT_CRYPTO", error.message);
    }
    plaintext.resize(written);

    nlohmann::json parsed =
        nlohmann::json::parse(plaintext.begin(), plaintext.end(), nullptr, false);
    hrtcore_secure_zero(plaintext.data(), plaintext.size());
    if (parsed.is_discarded() || !parsed.is_object()) {
        throw SnapshotCorruptError("snapshot plaintext is not valid JSON");
    }
    return parsed;
}

nlohmann::json encrypt_cloud_bundle(const nlohmann::json &payload,
                                    const std::array<uint8_t, 32> &key) {
    const std::string plaintext = payload.dump();
    std::array<uint8_t, kIvLen> iv{};
    hrtcore_error error{};
    if (hrtcore_random_bytes(iv.data(), iv.size(), &error) != HRTCORE_OK) {
        throw ToolError(ExitCode::Vault, "SNAPSHOT_CRYPTO", error.message);
    }

    const size_t sealed_len = plaintext.size() + kTagLen;
    std::vector<uint8_t> sealed(sealed_len);
    size_t written = 0;
    if (hrtcore_aes256gcm_encrypt(key.data(), key.size(), iv.data(), iv.size(), nullptr, 0,
                                  reinterpret_cast<const uint8_t *>(plaintext.data()),
                                  plaintext.size(), sealed.data(), sealed.size(), &written,
                                  &error) != HRTCORE_OK) {
        throw ToolError(ExitCode::Vault, "SNAPSHOT_CRYPTO", error.message);
    }
    if (written != sealed_len) {
        throw ToolError(ExitCode::Vault, "SNAPSHOT_CRYPTO",
                        "unexpected snapshot ciphertext length");
    }
    return nlohmann::json{
        {"cloud", kCloudVersion},
        {"iv", b64_encode(iv.data(), iv.size())},
        {"data", b64_encode(sealed.data(), sealed.size())},
    };
}

nlohmann::json load_snapshot(const std::filesystem::path &path,
                             const std::string &account_password,
                             const std::string &user_id) {
    std::error_code size_error;
    const auto size = std::filesystem::file_size(path, size_error);
    if (size_error) {
        throw ToolError(ExitCode::Vault, "SNAPSHOT_READ",
                        "cannot read the snapshot: " + size_error.message());
    }
    if (size > kMaxSnapshotBytes) {
        throw ToolLimitError("snapshot exceeds the 16 MiB limit");
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw ToolError(ExitCode::Vault, "SNAPSHOT_READ", "cannot open the snapshot");
    }
    const std::string text((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    nlohmann::json parsed = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        throw SnapshotCorruptError("snapshot is not valid JSON");
    }
    if (!is_cloud_bundle(parsed)) {
        return parsed;
    }
    if (account_password.empty() || user_id.empty()) {
        throw ToolError(ExitCode::Usage, "SNAPSHOT_CREDENTIALS",
                        "an account password and user id are required to decrypt this snapshot");
    }
    const std::array<uint8_t, kKeyLen> key = derive_cloud_key(account_password, user_id);
    return decrypt_cloud_bundle(parsed, key);
}

void save_snapshot(const std::filesystem::path &path, const nlohmann::json &payload,
                   const std::string &account_password, const std::string &user_id) {
    const std::array<uint8_t, kKeyLen> key = derive_cloud_key(account_password, user_id);
    const nlohmann::json bundle = encrypt_cloud_bundle(payload, key);
    write_atomic(path, bundle.dump());
}
