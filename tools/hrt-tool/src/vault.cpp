#include "vault.h"

#include "entropy.h"
#include "hrtcore/hrtcore.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
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

constexpr uint16_t kVaultVersion = 1;
constexpr uint16_t kKdfPbkdf2Sha256 = 1;
constexpr uint32_t kDefaultIterations = 600000;
constexpr uint32_t kMinIterations = 100000;
constexpr uint32_t kMaxIterations = 1000000;
constexpr size_t kSaltLen = 16;
constexpr size_t kNonceLen = 12;
constexpr size_t kKeyLen = 32;
constexpr size_t kTagLen = 16;
constexpr size_t kHeaderLen = 48;
constexpr size_t kMaxVaultBytes = 1024 * 1024;
constexpr size_t kMaxPlaintextBytes = kMaxVaultBytes - kHeaderLen - kTagLen;

int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

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

std::filesystem::path parent_or_dot(const std::filesystem::path &path) {
    return path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
}

void ensure_parent_dir(const std::filesystem::path &path) {
    const std::filesystem::path dir = parent_or_dot(path);
    const bool existed = std::filesystem::exists(dir);
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error && !std::filesystem::is_directory(dir)) {
        throw ToolError(ExitCode::Vault, "VAULT_WRITE",
                        "cannot create the vault directory: " + error.message());
    }
#ifndef _WIN32
    if (!existed) {
        ::chmod(dir.c_str(), 0700);
    }
#endif
}

void check_permissions(const std::filesystem::path &path) {
#ifndef _WIN32
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0) {
        throw ToolError(ExitCode::Vault, "VAULT_READ",
                        "cannot stat the vault: " + std::string(std::strerror(errno)));
    }
    if ((info.st_mode & 0077) != 0) {
        throw ToolError(ExitCode::Vault, "VAULT_PERMISSIONS",
                        "vault must not be group- or world-accessible; chmod 0600 it");
    }
#endif
}

std::vector<uint8_t> read_file(const std::filesystem::path &path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        throw ToolError(ExitCode::Vault, "VAULT_READ",
                        "cannot read the vault: " + error.message());
    }
    if (size > kMaxVaultBytes) {
        throw VaultCorruptError("vault is larger than 1 MiB");
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw ToolError(ExitCode::Vault, "VAULT_READ", "cannot open the vault");
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>());
}

void write_atomic(const std::filesystem::path &path, const std::vector<uint8_t> &bytes) {
    const std::filesystem::path dir = parent_or_dot(path);
#ifdef _WIN32
    const std::filesystem::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw ToolError(ExitCode::Vault, "VAULT_WRITE", "cannot create a temporary vault");
        }
        out.write(reinterpret_cast<const char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            throw ToolError(ExitCode::Vault, "VAULT_WRITE", "cannot write the temporary vault");
        }
    }
    std::error_code error;
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw ToolError(ExitCode::Vault, "VAULT_WRITE",
                        "cannot publish the vault: " + error.message());
    }
#else
    static std::atomic<unsigned> counter{0};
    const std::filesystem::path temp =
        dir / (path.filename().string() + ".tmp." + std::to_string(::getpid()) + "." +
               std::to_string(counter.fetch_add(1)));

    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        throw ToolError(ExitCode::Vault, "VAULT_WRITE",
                        "cannot create a temporary vault: " + std::string(std::strerror(errno)));
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
        throw ToolError(ExitCode::Vault, "VAULT_WRITE", "cannot write the vault: " + failure);
    }

    const int dir_fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }
#endif
}

nlohmann::json to_json(const VaultData &data) {
    return nlohmann::json{
        {"version", data.version},
        {"token", data.token},
        {"user_id", data.user_id},
        {"username", data.username},
        {"device_pass", data.device_pass},
        {"cloud_key", data.cloud_key},
        {"server", data.server},
        {"proxy", data.proxy},
        {"trust", data.trust},
        {"device_key_pem", data.device_key_pem},
        {"device_cert_pem", data.device_cert_pem},
        {"device_fingerprint", data.device_fingerprint},
        {"opsec_ack", data.opsec_ack},
        {"last_login_failed_at", data.last_login_failed_at},
        {"created_at", data.created_at},
        {"updated_at", data.updated_at},
    };
}

VaultData from_json(const nlohmann::json &value) {
    if (!value.is_object()) {
        throw VaultCorruptError("vault payload is not a JSON object");
    }
    VaultData data;
    data.version = value.value("version", 1);
    if (data.version != 1) {
        throw VaultCorruptError("unsupported vault payload version");
    }
    data.token = value.value("token", std::string{});
    data.user_id = value.value("user_id", std::string{});
    data.username = value.value("username", std::string{});
    data.device_pass = value.value("device_pass", std::string{});
    data.cloud_key = value.value("cloud_key", std::string{});
    data.server = value.value("server", std::string{"https://hrt.mahiro.uk"});
    data.proxy = value.value("proxy", std::string{});
    if (value.contains("trust") && value.at("trust").is_object()) {
        data.trust = value.at("trust");
    } else {
        data.trust = nlohmann::json::object();
    }
    data.device_key_pem = value.value("device_key_pem", std::string{});
    data.device_cert_pem = value.value("device_cert_pem", std::string{});
    data.device_fingerprint = value.value("device_fingerprint", std::string{});
    data.opsec_ack = value.value("opsec_ack", false);
    data.last_login_failed_at =
        value.value("last_login_failed_at", static_cast<int64_t>(0));
    data.created_at = value.value("created_at", static_cast<int64_t>(0));
    data.updated_at = value.value("updated_at", static_cast<int64_t>(0));
    return data;
}

std::array<uint8_t, kKeyLen> derive_key(const std::string &password, const uint8_t *salt,
                                        size_t salt_len, uint32_t iterations) {
    std::array<uint8_t, kKeyLen> key{};
    hrtcore_error error{};
    const int result = hrtcore_pbkdf2_sha256(
        reinterpret_cast<const uint8_t *>(password.data()), password.size(), salt, salt_len,
        iterations, key.data(), key.size(), &error);
    if (result != HRTCORE_OK) {
        throw ToolError(ExitCode::Vault, "VAULT_CRYPTO", error.message);
    }
    return key;
}

std::array<uint8_t, kKeyLen> derive_log_key(const std::array<uint8_t, kKeyLen> &key) {
    static const uint8_t info[] = "hrt-tool-log-v1";
    const uint8_t empty_salt = 0;
    std::array<uint8_t, kKeyLen> out{};
    hrtcore_error error{};
    const int result = hrtcore_hkdf_sha256(key.data(), key.size(), &empty_salt, 0, info,
                                           sizeof(info) - 1, out.data(), out.size(), &error);
    if (result != HRTCORE_OK) {
        throw ToolError(ExitCode::Vault, "VAULT_CRYPTO", error.message);
    }
    return out;
}

struct SealedVault {
    std::vector<uint8_t> bytes;
    std::array<uint8_t, kKeyLen> key;
};

struct HeaderParams {
    uint32_t iterations = kDefaultIterations;
    std::array<uint8_t, kSaltLen> salt{};
};

HeaderParams read_header_params(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw ToolError(ExitCode::Vault, "VAULT_READ", "cannot read the vault header");
    }
    uint8_t header[kHeaderLen] = {};
    in.read(reinterpret_cast<char *>(header), static_cast<std::streamsize>(kHeaderLen));
    if (in.gcount() != static_cast<std::streamsize>(kHeaderLen)) {
        throw VaultCorruptError("vault header is truncated");
    }
    if (std::memcmp(header, "HRTV", 4) != 0) {
        throw VaultCorruptError("vault magic is not HRTV");
    }
    if (get_u16(header + 4) != kVaultVersion || get_u16(header + 6) != kKdfPbkdf2Sha256) {
        throw VaultCorruptError("unsupported vault header");
    }

    HeaderParams params;
    params.iterations = get_u32(header + 8);
    if (params.iterations < kMinIterations || params.iterations > kMaxIterations) {
        throw VaultCorruptError("vault KDF iteration count is out of range");
    }
    if (get_u16(header + 12) != kSaltLen) {
        throw VaultCorruptError("vault salt length is invalid");
    }
    std::memcpy(params.salt.data(), header + 14, kSaltLen);
    return params;
}

SealedVault seal(const VaultData &data, const std::string &password,
                 const HeaderParams *reuse_salt = nullptr) {
    const std::string plaintext = to_json(data).dump();
    if (plaintext.size() > kMaxPlaintextBytes) {
        throw ToolError(ExitCode::Data, "VAULT_TOO_LARGE",
                        "vault payload is larger than the 1 MiB limit");
    }

    std::array<uint8_t, kSaltLen> salt{};
    uint32_t iterations = kDefaultIterations;
    hrtcore_error error{};
    if (reuse_salt != nullptr) {
        salt = reuse_salt->salt;
        iterations = reuse_salt->iterations;
    } else if (hrtcore_random_bytes(salt.data(), salt.size(), &error) != HRTCORE_OK) {
        throw ToolError(ExitCode::Vault, "VAULT_CRYPTO", error.message);
    }

    std::array<uint8_t, kNonceLen> nonce{};
    if (hrtcore_random_bytes(nonce.data(), nonce.size(), &error) != HRTCORE_OK) {
        throw ToolError(ExitCode::Vault, "VAULT_CRYPTO", error.message);
    }

    const std::array<uint8_t, kKeyLen> key =
        derive_key(password, salt.data(), salt.size(), iterations);

    uint8_t header[kHeaderLen] = {};
    std::memcpy(header, "HRTV", 4);
    put_u16(header + 4, kVaultVersion);
    put_u16(header + 6, kKdfPbkdf2Sha256);
    put_u32(header + 8, iterations);
    put_u16(header + 12, static_cast<uint16_t>(kSaltLen));
    std::memcpy(header + 14, salt.data(), kSaltLen);
    put_u16(header + 30, static_cast<uint16_t>(kNonceLen));
    std::memcpy(header + 32, nonce.data(), kNonceLen);

    const size_t ciphertext_len = plaintext.size() + kTagLen;
    put_u32(header + 44, static_cast<uint32_t>(ciphertext_len));

    std::vector<uint8_t> bytes(kHeaderLen + ciphertext_len);
    std::memcpy(bytes.data(), header, kHeaderLen);

    size_t written = 0;
    if (hrtcore_aes256gcm_encrypt(key.data(), key.size(), nonce.data(), nonce.size(), header,
                                  kHeaderLen,
                                  reinterpret_cast<const uint8_t *>(plaintext.data()),
                                  plaintext.size(), bytes.data() + kHeaderLen, ciphertext_len,
                                  &written, &error) != HRTCORE_OK) {
        throw ToolError(ExitCode::Vault, "VAULT_CRYPTO", error.message);
    }
    if (written != ciphertext_len) {
        throw ToolError(ExitCode::Vault, "VAULT_CRYPTO", "unexpected ciphertext length");
    }
    return SealedVault{std::move(bytes), key};
}

} // namespace

VaultSession::VaultSession(VaultData value, std::array<uint8_t, 32> key_value)
    : data(std::move(value)), key(key_value), log_key(derive_log_key(key_value)) {}

VaultSession::VaultSession(VaultSession &&other) noexcept
    : data(std::move(other.data)), key(other.key), log_key(other.log_key) {
    other.wipe();
}

VaultSession &VaultSession::operator=(VaultSession &&other) noexcept {
    if (this != &other) {
        wipe();
        data = std::move(other.data);
        key = other.key;
        log_key = other.log_key;
        other.wipe();
    }
    return *this;
}

VaultSession::~VaultSession() {
    wipe();
}

void VaultSession::wipe() {
    const auto wipe_string = [](std::string &value) {
        if (!value.empty()) {
            hrtcore_secure_zero(value.data(), value.size());
            value.clear();
        }
    };
    wipe_string(data.token);
    wipe_string(data.user_id);
    wipe_string(data.username);
    wipe_string(data.device_pass);
    wipe_string(data.cloud_key);
    hrtcore_secure_zero(key.data(), key.size());
    hrtcore_secure_zero(log_key.data(), log_key.size());
    key.fill(0);
    log_key.fill(0);
}

bool Vault::exists(const std::filesystem::path &path) {
    return std::filesystem::exists(path);
}

VaultSession Vault::create(const std::filesystem::path &path, const std::string &password,
                           bool overwrite) {
    if (password.size() > kMaxMasterPasswordBytes) {
        throw ToolLimitError("master password exceeds 256 bytes");
    }
    const EntropyEstimate estimate = estimate_entropy(password);
    if (!meets_master_password_floor(estimate)) {
        std::ostringstream message;
        message << "master password is too weak: ";
        if (estimate.weak_structure) {
            message << estimate.structure_reason << " (guess " << estimate.guess_bits
                    << " bits, uniform " << estimate.uniform_bits << " bits)";
        } else {
            message << "guess " << estimate.guess_bits << " bits, uniform "
                    << estimate.uniform_bits << " bits; at least 40 of each is required";
        }
        throw VaultWeakPasswordError(message.str(), estimate.guess_bits, estimate.uniform_bits);
    }
    if (!overwrite && exists(path)) {
        throw ToolError(ExitCode::Vault, "VAULT_EXISTS",
                        "a vault already exists; use --reset to replace it");
    }

    ensure_parent_dir(path);

    VaultData data;
    data.version = 1;
    data.created_at = now_seconds();
    data.updated_at = data.created_at;

    SealedVault sealed = seal(data, password);
    write_atomic(path, sealed.bytes);
    return VaultSession(std::move(data), sealed.key);
}

VaultSession Vault::unlock(const std::filesystem::path &path, const std::string &password) {
    check_permissions(path);
    const std::vector<uint8_t> bytes = read_file(path);
    if (bytes.size() < kHeaderLen) {
        throw VaultCorruptError("vault header is truncated");
    }
    if (std::memcmp(bytes.data(), "HRTV", 4) != 0) {
        throw VaultCorruptError("vault magic is not HRTV");
    }

    const uint16_t version = get_u16(bytes.data() + 4);
    const uint16_t kdf = get_u16(bytes.data() + 6);
    const uint32_t iterations = get_u32(bytes.data() + 8);
    const uint16_t salt_len = get_u16(bytes.data() + 12);
    const uint16_t nonce_len = get_u16(bytes.data() + 30);
    const uint32_t ciphertext_len = get_u32(bytes.data() + 44);

    if (version != kVaultVersion) {
        throw VaultCorruptError("unsupported vault version");
    }
    if (kdf != kKdfPbkdf2Sha256) {
        throw VaultCorruptError("unsupported vault KDF");
    }
    if (iterations < kMinIterations || iterations > kMaxIterations) {
        throw VaultCorruptError("vault KDF iteration count is out of range");
    }
    if (salt_len != kSaltLen || nonce_len != kNonceLen) {
        throw VaultCorruptError("vault salt or nonce length is invalid");
    }
    if (ciphertext_len < kTagLen || bytes.size() != kHeaderLen + ciphertext_len) {
        throw VaultCorruptError("vault ciphertext length does not match the file");
    }

    const std::array<uint8_t, kKeyLen> key =
        derive_key(password, bytes.data() + 14, salt_len, iterations);

    const size_t plaintext_cap = ciphertext_len - kTagLen;
    std::vector<uint8_t> plaintext(plaintext_cap);
    size_t plaintext_len = 0;
    hrtcore_error error{};
    const int result = hrtcore_aes256gcm_decrypt(
        key.data(), key.size(), bytes.data() + 32, nonce_len, bytes.data(), kHeaderLen,
        bytes.data() + kHeaderLen, ciphertext_len, plaintext.data(), plaintext.size(),
        &plaintext_len, &error);
    if (result != HRTCORE_OK) {
        hrtcore_secure_zero(plaintext.data(), plaintext.size());
        if (result == HRTCORE_ERR_AUTH) {
            throw VaultAuthError("vault password is wrong or the vault was tampered with");
        }
        throw ToolError(ExitCode::Vault, "VAULT_CRYPTO", error.message);
    }
    plaintext.resize(plaintext_len);

    nlohmann::json parsed = nlohmann::json::parse(plaintext.begin(), plaintext.end(), nullptr,
                                                  false);
    hrtcore_secure_zero(plaintext.data(), plaintext.size());
    if (parsed.is_discarded() || !parsed.is_object()) {
        throw VaultCorruptError("vault payload is not valid JSON");
    }

    VaultData data = from_json(parsed);
    return VaultSession(std::move(data), key);
}

void Vault::save(const std::filesystem::path &path, const VaultData &data,
                 const std::string &password) {
    if (password.size() > kMaxMasterPasswordBytes) {
        throw ToolLimitError("master password exceeds 256 bytes");
    }
    if (exists(path)) {
        // Verify the password before replacing an existing vault.
        VaultSession verified = unlock(path, password);
        (void)verified;
    }
    ensure_parent_dir(path);

    VaultData updated = data;
    updated.updated_at = now_seconds();

    if (exists(path)) {
        VaultSession verified = unlock(path, password);
        (void)verified;
        const HeaderParams params = read_header_params(path);
        SealedVault sealed = seal(updated, password, &params);
        write_atomic(path, sealed.bytes);
        return;
    }

    SealedVault sealed = seal(updated, password);
    write_atomic(path, sealed.bytes);
}
