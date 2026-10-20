#include "net_error.h"

#include <atomic>
#include <cerrno>
#include <curl/curl.h>

FailureKind classify_errno(int errnum) {
    switch (errnum) {
    case ECONNRESET:
    case EPIPE:
#ifdef ECONNABORTED
    case ECONNABORTED:
#endif
        return FailureKind::TunnelInterrupted;
    case ETIMEDOUT:
        return FailureKind::Timeout;
    case ECONNREFUSED:
#ifdef EHOSTUNREACH
    case EHOSTUNREACH:
#endif
#ifdef ENETUNREACH
    case ENETUNREACH:
#endif
        return FailureKind::ServerInitiated;
    default:
        return FailureKind::None;
    }
}

FailureKind classify_curl(int curl_result, int errnum) {
    switch (curl_result) {
    case CURLE_OPERATION_TIMEDOUT:
        return FailureKind::Timeout;
    case CURLE_COULDNT_RESOLVE_PROXY:
        return FailureKind::ProxyDown;
    case CURLE_COULDNT_CONNECT: {
        const FailureKind by_errno = classify_errno(errnum);
        return by_errno == FailureKind::None ? FailureKind::TunnelInterrupted
                                             : by_errno;
    }
    case CURLE_RECV_ERROR:
    case CURLE_SEND_ERROR:
    case CURLE_GOT_NOTHING:
    case CURLE_PARTIAL_FILE:
        return FailureKind::TunnelInterrupted;
    case CURLE_SSL_CONNECT_ERROR:
        return FailureKind::TunnelInterrupted;
    case CURLE_COULDNT_RESOLVE_HOST:
        return FailureKind::ServerInitiated;
#ifdef CURLE_PROXY
    case CURLE_PROXY:
        return FailureKind::TunnelInterrupted;
#endif
    default:
        return FailureKind::None;
    }
}

FailureKind classify_http(long status) {
    if (status >= 500 && status <= 599) {
        return FailureKind::ServerInitiated;
    }
    return FailureKind::None;
}

FailureKind classify_socks5(unsigned char reply) {
    switch (reply) {
    case 0x00:
        return FailureKind::None;
    case 0x05:
        return FailureKind::ServerInitiated;
    case 0x02:
    case 0x07:
    case 0x08:
        return FailureKind::ServerInitiated;
    case 0x01:
    case 0x03:
    case 0x04:
    case 0x06:
        return FailureKind::TunnelInterrupted;
    default:
        return FailureKind::None;
    }
}

bool should_retry(FailureKind kind, bool is_read, bool debug, int attempts) {
    return kind == FailureKind::TunnelInterrupted && is_read && !debug &&
           attempts < 2;
}

bool should_print_human_line(bool debug, bool trace, bool no_humor, bool humor) {
    return !debug && !trace && !no_humor && humor;
}

namespace {
std::atomic<bool> g_humor{true};
}

void set_humor_enabled(bool enabled) { g_humor.store(enabled); }
bool humor_enabled() { return g_humor.load(); }

std::string mutation_check_hint(const std::string &command) {
    if (command == "lab") {
        return "the write may have been applied; run 'hrt-tool lab list' before "
               "retrying";
    }
    if (command == "weight") {
        return "the write may have been applied; run 'hrt-tool weight get' "
               "before retrying";
    }
    if (command == "add" || command == "dose") {
        return "the write may have been applied; run 'hrt-tool dose list' "
               "before retrying";
    }
    return "the write may have been applied; check the account before retrying";
}

std::string failure_human_line(Language language, FailureKind kind) {
    switch (kind) {
    case FailureKind::TunnelInterrupted:
        return tr(language, I18nKey::TunnelInterrupted);
    case FailureKind::ServerInitiated:
        return tr(language, I18nKey::ServerInitiated);
    case FailureKind::Timeout:
        return tr(language, I18nKey::Timeout);
    case FailureKind::ProxyDown:
        return tr(language, I18nKey::ProxyDown);
    case FailureKind::FinalFallback:
        return tr(language, I18nKey::FinalFallback);
    case FailureKind::None:
        return {};
    }
    return {};
}
