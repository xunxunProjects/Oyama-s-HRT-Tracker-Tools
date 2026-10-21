#include "link.h"
#include "test_framework.h"
#include "vault.h"

int main() {
    VaultData d;
    d.links[""] = LinkData{};
    d.links[""].url = "https://a.example";
    d.links[""].token = "tok-a";
    d.active_link = "";
    d.server = "https://a.example";
    d.token = "tok-a";

    LinkData mtf;
    mtf.url = "hrt://b.example:9443";
    mtf.proxy = "socks5h://127.0.0.1:9050";
    mtf.mode = "transfem";
    add_link(d, "mtf", mtf, false);
    CHECK(d.links.count("mtf") == 1);
    CHECK_THROWS(add_link(d, "mtf", mtf, false), ToolError);

    select_link(d, "mtf");
    CHECK_EQ(d.active_link, std::string("mtf"));
    CHECK_EQ(d.server, std::string("hrt://b.example:9443"));
    CHECK_EQ(d.mode, std::string("transfem"));
    CHECK_EQ(d.token, std::string(""));
    CHECK_EQ(d.links[""].token, std::string("tok-a"));

    select_link(d, "");
    CHECK_EQ(d.token, std::string("tok-a"));
    CHECK_EQ(d.links["mtf"].mode, std::string("transfem"));

    rename_link(d, "mtf", "main");
    CHECK(d.links.count("main") == 1);
    remove_link(d, "main", false);
    CHECK(d.links.count("main") == 0);
    CHECK_THROWS(remove_link(d, "", false), ToolError);

    LinkData secret = d.links[""];
    secret.token = "tok-b";
    secret.device_key_pem = "PRIVATE-KEY";
    const std::string summary = link_summary("base", secret);
    CHECK(summary.find("tok-b") == std::string::npos);
    CHECK(summary.find("PRIVATE-KEY") == std::string::npos);
    CHECK(summary.find("token: set") != std::string::npos);
    return test_result();
}
