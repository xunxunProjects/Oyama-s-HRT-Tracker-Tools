#pragma once

#include "error.h"
#include "i18n.h"

#include <string>
#include <utility>

enum class FailureKind {
    None,
    TunnelInterrupted,
    ServerInitiated,
    Timeout,
    ProxyDown,
    FinalFallback,
};

class NetworkFailure : public ToolError {
public:
    NetworkFailure(std::string code, std::string message, FailureKind kind)
        : ToolError(ExitCode::Network, std::move(code), std::move(message)),
          kind_(kind) {}

    FailureKind kind() const noexcept { return kind_; }

private:
    FailureKind kind_;
};

FailureKind classify_curl(int curl_result, int errnum);
FailureKind classify_errno(int errnum);
FailureKind classify_http(long status);
FailureKind classify_socks5(unsigned char reply);
bool should_retry(FailureKind kind, bool is_read, bool debug, int attempts);
bool should_print_human_line(bool debug, bool trace, bool no_humor, bool humor);
void set_humor_enabled(bool enabled);
bool humor_enabled();
std::string failure_human_line(Language language, FailureKind kind);
std::string mutation_check_hint(const std::string &command);
