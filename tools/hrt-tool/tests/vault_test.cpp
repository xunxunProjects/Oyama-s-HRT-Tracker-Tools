#include "error.h"
#include "log.h"
#include "paths.h"
#include "test_framework.h"
#include "vault.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

int g_temp_counter = 0;

class TempDir {
public:
    TempDir() {
        const char *base = std::getenv("TMPDIR");
        const std::filesystem::path root =
            base != nullptr && *base != '\0' ? std::filesystem::path(base)
                                             : std::filesystem::temp_directory_path();
        while (true) {
            path_ = root / ("hrt-tool-vault-test-" + std::to_string(++g_temp_counter));
            if (std::filesystem::create_directories(path_)) {
                break;
            }
        }
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;
    const std::filesystem::path &path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::vector<uint8_t> read_bytes(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>());
}

void write_bytes(const std::filesystem::path &path, const std::vector<uint8_t> &bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

bool all_zero(const std::array<uint8_t, 32> &bytes) {
    return std::all_of(bytes.begin(), bytes.end(), [](uint8_t value) { return value == 0; });
}

const char *kPassword = "correct horse battery staple";

} // namespace

int main() {
    {
        TempDir temp;
        const std::filesystem::path vault_path = temp.path() / "vault.hrtv";

        CHECK(!Vault::exists(vault_path));
        VaultSession created = Vault::create(vault_path, kPassword);
        CHECK(Vault::exists(vault_path));
        CHECK(!all_zero(created.key));
        CHECK(!all_zero(created.log_key));

        const std::vector<uint8_t> raw = read_bytes(vault_path);
        CHECK(raw.size() > 48);
        CHECK(raw.size() <= 1024 * 1024);
        CHECK(std::string(reinterpret_cast<const char *>(raw.data()), 4) == "HRTV");

        const std::string raw_text(reinterpret_cast<const char *>(raw.data()), raw.size());
        CHECK(raw_text.find("token") == std::string::npos);
        CHECK(raw_text.find("user-123") == std::string::npos);
        CHECK(raw_text.find(kPassword) == std::string::npos);

#ifndef _WIN32
        struct stat info {};
        CHECK(::stat(vault_path.c_str(), &info) == 0);
        CHECK((info.st_mode & 0777) == 0600);
#endif

        VaultSession unlocked = Vault::unlock(vault_path, kPassword);
        CHECK(unlocked.data.version == 1);
        CHECK(unlocked.data.server == "https://hrt.mahiro.uk");
        CHECK(unlocked.data.created_at > 0);
        CHECK(!all_zero(unlocked.key));
        CHECK(!all_zero(unlocked.log_key));

        CHECK_THROWS(Vault::unlock(vault_path, "wrong horse battery staple"), VaultAuthError);

        std::vector<uint8_t> tampered = raw;
        tampered.back() ^= 0x01;
        write_bytes(vault_path, tampered);
        CHECK_THROWS(Vault::unlock(vault_path, kPassword), VaultAuthError);

        std::vector<uint8_t> header_tampered = raw;
        // Flip the low byte of the iteration count so it stays inside the
        // accepted KDF range; the AAD then makes GCM authentication fail.
        header_tampered[11] ^= 0x01;
        write_bytes(vault_path, header_tampered);
        CHECK_THROWS(Vault::unlock(vault_path, kPassword), VaultAuthError);

        std::vector<uint8_t> salt_tampered = raw;
        salt_tampered[14] ^= 0x01;
        write_bytes(vault_path, salt_tampered);
        CHECK_THROWS(Vault::unlock(vault_path, kPassword), VaultAuthError);

        write_bytes(vault_path, raw);
        VaultSession session = Vault::unlock(vault_path, kPassword);
        session.data.token = "tok-abc";
        session.data.username = "user-123";
        Vault::save(vault_path, session.data, kPassword);

        VaultSession reloaded = Vault::unlock(vault_path, kPassword);
        CHECK(reloaded.data.token == "tok-abc");
        CHECK(reloaded.data.username == "user-123");

        for (const auto &entry : std::filesystem::directory_iterator(temp.path())) {
            CHECK(entry.path().filename().string().find(".tmp") == std::string::npos);
        }

        const std::vector<uint8_t> before_failed_save = read_bytes(vault_path);
#ifndef _WIN32
        std::filesystem::permissions(temp.path(),
                                     std::filesystem::perms::owner_read |
                                         std::filesystem::perms::owner_exec,
                                     std::filesystem::perm_options::replace);
        reloaded.data.token = "should-not-land";
        CHECK_THROWS(Vault::save(vault_path, reloaded.data, kPassword), ToolError);
        std::filesystem::permissions(temp.path(),
                                     std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace);
        CHECK(read_bytes(vault_path) == before_failed_save);
        VaultSession after_failed_save = Vault::unlock(vault_path, kPassword);
        CHECK(after_failed_save.data.token == "tok-abc");
#endif
    }

    {
        TempDir temp;
        const std::filesystem::path vault_path = temp.path() / "vault.hrtv";
        VaultSession session = Vault::create(vault_path, kPassword);
        const std::array<uint8_t, 32> key_before = session.key;

        const std::filesystem::path log_path = temp.path() / "log.bin";
        EncryptedLog::append(log_path, session.log_key, "before save");

        session.data.token = "after-save";
        Vault::save(vault_path, session.data, kPassword);

        VaultSession reloaded = Vault::unlock(vault_path, kPassword);
        CHECK(reloaded.key == key_before);
        CHECK(reloaded.data.token == "after-save");

        std::ostringstream shown;
        try {
            EncryptedLog::show(log_path, reloaded.log_key, shown);
            CHECK_EQ(shown.str(), std::string("before save\n"));
        } catch (const ToolError &error) {
            std::cerr << "log key changed across save: " << error.what() << "\n";
            ++g_failures;
        }
    }

    {
        TempDir temp;
        const std::filesystem::path vault_path = temp.path() / "vault.hrtv";
        CHECK_THROWS(Vault::create(vault_path, "aaaaaaaaaaaaaaaa"), VaultWeakPasswordError);
        CHECK(!Vault::exists(vault_path));
    }

    {
        TempDir temp;
        const std::filesystem::path vault_path = temp.path() / "vault.hrtv";
        Vault::create(vault_path, kPassword);
#ifndef _WIN32
        std::filesystem::permissions(vault_path, std::filesystem::perms::owner_read |
                                                    std::filesystem::perms::owner_write |
                                                    std::filesystem::perms::group_read |
                                                    std::filesystem::perms::others_read,
                                     std::filesystem::perm_options::replace);
        CHECK_THROWS(Vault::unlock(vault_path, kPassword), ToolError);
#endif
    }

    {
        TempDir temp;
        const std::filesystem::path vault_path = temp.path() / "vault.hrtv";
        std::vector<uint8_t> oversized(1024 * 1024 + 1, 0);
        write_bytes(vault_path, oversized);
#ifndef _WIN32
        ::chmod(vault_path.c_str(), 0600);
#endif
        CHECK_THROWS(Vault::unlock(vault_path, kPassword), ToolError);
    }

    {
        TempDir temp;
        const std::filesystem::path vault_path = temp.path() / "vault.hrtv";
        write_bytes(vault_path, std::vector<uint8_t>{'H', 'R', 'T', 'V'});
#ifndef _WIN32
        ::chmod(vault_path.c_str(), 0600);
#endif
        CHECK_THROWS(Vault::unlock(vault_path, kPassword), ToolError);
    }

    {
        TempDir temp;
#ifndef _WIN32
        const std::string original_xdg = std::getenv("XDG_CONFIG_HOME") == nullptr
                                             ? ""
                                             : std::getenv("XDG_CONFIG_HOME");
        const std::string xdg = temp.path().string();
        ::setenv("XDG_CONFIG_HOME", xdg.c_str(), 1);
        const Paths paths = resolve_paths("");
        CHECK(paths.config_dir == std::filesystem::path(xdg) / "hrt-tool");
        CHECK(paths.vault_file == std::filesystem::path(xdg) / "hrt-tool" / "vault.hrtv");
        if (original_xdg.empty()) {
            ::unsetenv("XDG_CONFIG_HOME");
        } else {
            ::setenv("XDG_CONFIG_HOME", original_xdg.c_str(), 1);
        }
#endif
        const std::filesystem::path explicit_dir = temp.path() / "explicit";
        const Paths explicit_paths = resolve_paths(explicit_dir.string());
        CHECK(explicit_paths.config_dir == explicit_dir);
        CHECK(explicit_paths.vault_file == explicit_dir / "vault.hrtv");
    }

    return test_result();
}
