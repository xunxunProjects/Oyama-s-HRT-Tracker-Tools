#include "error.h"
#include "i18n.h"
#include "test_framework.h"

#include <string>

int main() {
    CHECK(tr(Language::En, I18nKey::Welcome).find("hrt-tool") != std::string::npos);
    CHECK(tr(Language::En, I18nKey::TunnelInterrupted)
              .find("Tunnel dropped. Fuck!") != std::string::npos);
    CHECK(tr(Language::En, I18nKey::FinalFallback)
              .find("Oh no, the other side") != std::string::npos);
    CHECK(tr(Language::Ru, I18nKey::Welcome).find("hrt-tool") != std::string::npos);
    CHECK(tr(Language::Ru, I18nKey::TunnelInterrupted).find("Туннель") !=
          std::string::npos);
    CHECK_THROWS(parse_language("zh"), ToolError);
    for (I18nKey key : {I18nKey::Welcome, I18nKey::HelpHeader,
                        I18nKey::TunnelInterrupted}) {
        const std::string text = tr(Language::En, key) + tr(Language::Ru, key);
        CHECK(text.find("的") == std::string::npos); // no CJK
    }
    return test_result();
}
