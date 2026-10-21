#include "vault.h"
#include "test_framework.h"

int main() {
    nlohmann::json v1 = {
        {"version", 1}, {"token", "tok"}, {"user_id", "u1"},
        {"username", "alice"}, {"server", "https://a.example"},
        {"proxy", ""}, {"trust", nlohmann::json::object()},
    };
    VaultData d = vault_data_from_json(v1);
    CHECK_EQ(d.version, 2);
    CHECK_EQ(d.active_link, std::string(""));
    CHECK(d.links.count("") == 1);
    CHECK_EQ(d.links[""].token, std::string("tok"));
    CHECK_EQ(d.links[""].url, std::string("https://a.example"));
    CHECK_EQ(d.token, std::string("tok"));

    d.links["mtf"] = LinkData{};
    d.links["mtf"].url = "hrt://b.example:9443";
    d.links["mtf"].token = "tok-b";
    d.links["mtf"].mode = "transfem";
    d.active_link = "mtf";
    d.token = "tok-b";
    d.server = "hrt://b.example:9443";
    d.mode = "transfem";
    nlohmann::json out = vault_data_to_json(d);
    CHECK_EQ(out.at("version").get<int>(), 2);
    VaultData back = vault_data_from_json(out);
    CHECK_EQ(back.active_link, std::string("mtf"));
    CHECK_EQ(back.token, std::string("tok-b"));
    CHECK_EQ(back.links.size(), size_t{2});
    CHECK_EQ(back.links[""].token, std::string("tok"));
    return test_result();
}
