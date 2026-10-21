#include "i18n.h"

#include "error.h"

namespace {

struct Entry {
    const char *en;
    const char *ru;
};

const Entry kEntries[] = {
    {"hrt-tool console. Type 'help' for commands, 'exit' to quit.",
     "Консоль hrt-tool. Введите 'help' для списка команд, 'exit' для выхода."},
    {"Commands:", "Команды:"},
    {"Type 'help <command>' for details.", "Введите 'help <command>' для подробностей."},
    {"Type 'help' for commands, 'exit' to leave.",
     "Введите 'help' для команд, 'exit' для выхода."},
    {"unknown command", "неизвестная команда"},
    {"Type 'exit' to leave.", "Введите 'exit', чтобы выйти."},
    {"init is not available inside the console; leave and run hrt-tool init.",
     "init недоступен внутри консоли; выйдите и запустите hrt-tool init."},
    {"The connection was unexpectedly closed or reset.\n"
     "Tunnel dropped. Fuck! Disconnecting at a time like this? Fine — let me "
     "reconnect, and I’ll settle the score.",
     "Соединение неожиданно закрыто или сброшено.\n"
     "Туннель упал. Чёрт! Отключаться именно сейчас? Ладно — переподключусь и "
     "поквитаюсь."},
    {"The server rejected the connection.\n"
     "The server’s not playing nice today. Let me check the error logs.",
     "Сервер отклонил соединение.\n"
     "Сервер сегодня не в настроении. Проверю логи ошибок."},
    {"Still waiting for the tunnel...\n"
     "Hello? Is anyone there on the other end of the tunnel? Let’s try again.",
     "Всё ещё жду туннель...\n"
     "Алло? Есть кто-нибудь на другом конце туннеля? Давай попробуем ещё раз."},
    {"The proxy isn't answering. Is the daemon actually running, or did it "
     "wander off?",
     "Прокси не отвечает. Демон точно запущен или ушёл погулять?"},
    {"Oh no, the other side doesn't want us anymore (cries).",
     "О нет, другая сторона больше не хочет с нами разговаривать (плачет)."},
};

constexpr size_t kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);

} // namespace

Language parse_language(const std::string &value) {
    if (value == "en") {
        return Language::En;
    }
    if (value == "ru") {
        return Language::Ru;
    }
    throw ToolError(ExitCode::Config, "CONFIG_VALUE",
                    "language must be en or ru");
}

const char *language_name(Language language) {
    return language == Language::Ru ? "ru" : "en";
}

std::string tr(Language language, I18nKey key) {
    const size_t index = static_cast<size_t>(key);
    if (index >= kEntryCount) {
        return {};
    }
    return language == Language::Ru ? kEntries[index].ru : kEntries[index].en;
}
