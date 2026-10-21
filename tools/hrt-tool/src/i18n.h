#pragma once

#include <string>

enum class Language { En, Ru };

Language parse_language(const std::string &value); // en|ru, throws Config
const char *language_name(Language language);      // "en"|"ru"

enum class I18nKey {
    Welcome,
    HelpHeader,
    HelpFooter,
    PromptHelp,
    UnknownCommand,
    ExitHint,
    ConsoleInitRefused,
    TunnelInterrupted,
    ServerInitiated,
    Timeout,
    ProxyDown,
    FinalFallback,
};

std::string tr(Language language, I18nKey key);
