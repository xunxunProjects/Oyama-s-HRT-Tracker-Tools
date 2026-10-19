#include "error.h"
#include "snapshot.h"
#include "test_framework.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#ifndef HRT_TOOL_TESTDATA_DIR
#error "HRT_TOOL_TESTDATA_DIR must be defined"
#endif

#ifndef _WIN32
#include <sys/stat.h>
#endif

using nlohmann::json;

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
            path_ = root / ("hrt-tool-snapshot-test-" + std::to_string(++g_temp_counter));
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

void write_text(const std::filesystem::path &path, const std::string &text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

} // namespace

int main() {
    std::ifstream fixture_file(std::string(HRT_TOOL_TESTDATA_DIR) + "/pk-vectors.json");
    if (!fixture_file) {
        std::cerr << "cannot open pk-vectors.json\n";
        return 1;
    }
    json fixture;
    fixture_file >> fixture;
    const json &snapshot = fixture.at("snapshot");
    const std::string user_id = snapshot.at("userId").get<std::string>();
    const std::string password = snapshot.at("password").get<std::string>();
    const json bundle = snapshot.at("bundle");
    const json payload = snapshot.at("payload");

    const std::array<uint8_t, 32> key = derive_cloud_key(password, user_id);
    CHECK(key == derive_cloud_key(password, user_id));
    CHECK(!(key == derive_cloud_key(password, "other-user")));

    const json decrypted = decrypt_cloud_bundle(bundle, key);
    CHECK(decrypted == payload);

    const json round_trip = encrypt_cloud_bundle(payload, key);
    CHECK(decrypt_cloud_bundle(round_trip, key) == payload);

    const std::array<uint8_t, 32> wrong_key =
        derive_cloud_key("wrong horse battery staple", user_id);
    CHECK_THROWS(decrypt_cloud_bundle(bundle, wrong_key), ToolError);

    {
        json tampered = bundle;
        std::string data = tampered.at("data").get<std::string>();
        const std::string::size_type pos = data.find_last_not_of('=');
        data[pos] = data[pos] == 'A' ? 'B' : 'A';
        tampered["data"] = data;
        CHECK_THROWS(decrypt_cloud_bundle(tampered, key), ToolError);
    }
    {
        json tampered = bundle;
        std::string iv = tampered.at("iv").get<std::string>();
        iv[0] = iv[0] == 'A' ? 'B' : 'A';
        tampered["iv"] = iv;
        CHECK_THROWS(decrypt_cloud_bundle(tampered, key), ToolError);
    }

    {
        TempDir temp;
        const std::filesystem::path path = temp.path() / "plain.json";
        write_text(path, payload.dump());
        CHECK(load_snapshot(path, "", "") == payload);
    }

    {
        TempDir temp;
        const std::filesystem::path path = temp.path() / "snapshot.json";
        save_snapshot(path, payload, password, user_id);
        CHECK(std::filesystem::exists(path));

        const std::vector<uint8_t> raw = read_bytes(path);
        const std::string raw_text(reinterpret_cast<const char *>(raw.data()), raw.size());
        CHECK(raw_text.find("transfem") == std::string::npos);
        CHECK(raw_text.find("\"e1\"") == std::string::npos);

#ifndef _WIN32
        struct stat info {};
        CHECK(::stat(path.c_str(), &info) == 0);
        CHECK((info.st_mode & 0777) == 0600);
#endif

        CHECK(load_snapshot(path, password, user_id) == payload);
        CHECK_THROWS(load_snapshot(path, "wrong horse battery staple", user_id), ToolError);
    }

    {
        TempDir temp;
        const std::filesystem::path path = temp.path() / "bundle.json";
        write_text(path, bundle.dump());
        CHECK_THROWS(load_snapshot(path, "", ""), ToolError);
    }

    {
        TempDir temp;
        const std::filesystem::path path = temp.path() / "oversized.json";
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            std::vector<char> zeros(16 * 1024 * 1024 + 1, '\0');
            out.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
        }
        CHECK_THROWS(load_snapshot(path, password, user_id), ToolError);
    }

    return test_result();
}
