#include "log.h"
#include "test_framework.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
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
            path_ = root / ("hrt-tool-log-test-" + std::to_string(++g_temp_counter));
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

std::array<uint8_t, 32> make_key(uint8_t seed) {
    std::array<uint8_t, 32> key{};
    for (size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<uint8_t>(seed + i);
    }
    return key;
}

} // namespace

int main() {
    const std::array<uint8_t, 32> key = make_key(0);
    const std::array<uint8_t, 32> other_key = make_key(0x80);

    {
        TempDir temp;
        const std::filesystem::path path = temp.path() / "log.bin";
        EncryptedLog::append(path, key, "first");
        EncryptedLog::append(path, key, "second");
        EncryptedLog::append(path, key, "third\n");

        std::ostringstream shown;
        EncryptedLog::show(path, key, shown);
        CHECK_EQ(shown.str(), std::string("first\nsecond\nthird\n"));

        const std::vector<uint8_t> raw = read_bytes(path);
        const std::string raw_text(reinterpret_cast<const char *>(raw.data()), raw.size());
        CHECK(raw_text.find("first") == std::string::npos);
        CHECK(raw_text.find("second") == std::string::npos);

#ifndef _WIN32
        struct stat info {};
        CHECK(::stat(path.c_str(), &info) == 0);
        CHECK((info.st_mode & 0777) == 0600);
#endif

        std::ostringstream wrong;
        CHECK_THROWS(EncryptedLog::show(path, other_key, wrong), LogAuthError);
    }

    {
        TempDir temp;
        const std::filesystem::path path = temp.path() / "log.bin";
        EncryptedLog::append(path, key, "secret");
        std::vector<uint8_t> raw = read_bytes(path);
        raw.back() ^= 0x01;
        write_bytes(path, raw);
        std::ostringstream shown;
        CHECK_THROWS(EncryptedLog::show(path, key, shown), LogAuthError);
    }

    {
        TempDir temp;
        const std::filesystem::path path = temp.path() / "log.bin";
        EncryptedLog::append(path, key, "secret");
        std::vector<uint8_t> raw = read_bytes(path);
        raw.pop_back();
        write_bytes(path, raw);
        std::ostringstream shown;
        CHECK_THROWS(EncryptedLog::show(path, key, shown), LogCorruptError);
    }

    {
        TempDir temp;
        const std::filesystem::path path = temp.path() / "log.bin";
        const std::string oversized(64 * 1024 + 1, 'x');
        CHECK_THROWS(EncryptedLog::append(path, key, oversized), ToolLimitError);
    }

    {
        TempDir temp;
        const std::filesystem::path path = temp.path() / "log.bin";
        write_bytes(path, {});
        std::ostringstream shown;
        EncryptedLog::show(path, key, shown);
        CHECK(shown.str().empty());
    }

    {
        TempDir temp;
        const std::filesystem::path path = temp.path() / "log.bin";
        write_bytes(path, {0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0});
        std::ostringstream shown;
        CHECK_THROWS(EncryptedLog::show(path, key, shown), LogCorruptError);
    }

    {
        TempDir temp;
        const std::filesystem::path path = temp.path() / "log.bin";
        std::vector<uint8_t> bytes = {0, 0, 0, 100};
        bytes.resize(4 + 10, 0);
        write_bytes(path, bytes);
        std::ostringstream shown;
        CHECK_THROWS(EncryptedLog::show(path, key, shown), LogCorruptError);
    }

    return test_result();
}
