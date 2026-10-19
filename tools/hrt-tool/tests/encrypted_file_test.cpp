#include "encrypted_file.h"
#include "error.h"
#include "test_framework.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
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
            path_ = root / ("hrt-tool-export-test-" + std::to_string(++g_temp_counter));
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

} // namespace

int main() {
    const std::string password = "correct horse battery staple";
    const std::string plaintext = "{\"format\":\"sync\",\"value\":42}";

    {
        TempDir temp;
        const std::filesystem::path path = temp.path() / "export.hrte";
        seal_encrypted_file(path, "HRTE", plaintext, password);
        CHECK_EQ(open_encrypted_file(path, "HRTE", password), plaintext);

        const std::vector<uint8_t> raw = read_bytes(path);
        const std::string raw_text(reinterpret_cast<const char *>(raw.data()), raw.size());
        CHECK(raw_text.find("format") == std::string::npos);
        CHECK(raw_text.find("value") == std::string::npos);

#ifndef _WIN32
        struct stat info {};
        CHECK(::stat(path.c_str(), &info) == 0);
        CHECK((info.st_mode & 0777) == 0600);
#endif

        CHECK_THROWS(open_encrypted_file(path, "HRTE", "wrong horse battery staple"),
                     ToolError);
        CHECK_THROWS(open_encrypted_file(path, "HRTV", password), ToolError);

        std::vector<uint8_t> tampered = raw;
        tampered.back() ^= 0x01;
        write_bytes(path, tampered);
        CHECK_THROWS(open_encrypted_file(path, "HRTE", password), ToolError);
    }

    {
        TempDir temp;
        const std::filesystem::path path = temp.path() / "oversized.hrte";
        std::vector<uint8_t> zeros(16 * 1024 * 1024 + 1, 0);
        write_bytes(path, zeros);
#ifndef _WIN32
        ::chmod(path.c_str(), 0600);
#endif
        CHECK_THROWS(open_encrypted_file(path, "HRTE", password), ToolError);
    }

    return test_result();
}
