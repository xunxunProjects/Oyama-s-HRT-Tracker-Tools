#include "error.h"
#include "net_error.h"
#include "test_framework.h"

#include <cerrno>
#include <curl/curl.h>
#include <string>

int main() {
    CHECK(classify_curl(CURLE_RECV_ERROR, ECONNRESET) ==
          FailureKind::TunnelInterrupted);
    CHECK(classify_curl(CURLE_SEND_ERROR, EPIPE) == FailureKind::TunnelInterrupted);
    CHECK(classify_curl(CURLE_COULDNT_CONNECT, ECONNREFUSED) ==
          FailureKind::ServerInitiated);
    CHECK(classify_curl(CURLE_OPERATION_TIMEDOUT, 0) == FailureKind::Timeout);
    CHECK(classify_http(502) == FailureKind::ServerInitiated);
    CHECK(classify_http(503) == FailureKind::ServerInitiated);
    CHECK(classify_http(500) == FailureKind::ServerInitiated);
    CHECK(classify_socks5(0x05) == FailureKind::ServerInitiated);
    CHECK(classify_socks5(0x04) == FailureKind::TunnelInterrupted);
    CHECK(should_retry(FailureKind::TunnelInterrupted, true, false, 0));
    CHECK(!should_retry(FailureKind::TunnelInterrupted, true, true, 0));
    CHECK(!should_retry(FailureKind::TunnelInterrupted, false, false, 0));
    CHECK(!should_retry(FailureKind::TunnelInterrupted, true, false, 2));
    CHECK(!should_retry(FailureKind::ServerInitiated, true, false, 0));
    CHECK(!should_retry(FailureKind::ProxyDown, true, false, 0));
    CHECK(should_print_human_line(false, false, false, true));
    CHECK(!should_print_human_line(true, false, false, true));
    CHECK(!should_print_human_line(false, true, false, true));
    CHECK(!should_print_human_line(false, false, true, true));
    CHECK(!should_print_human_line(false, false, false, false));
    set_humor_enabled(false);
    CHECK(!should_print_human_line(false, false, false, humor_enabled()));
    set_humor_enabled(true);
    CHECK(should_print_human_line(false, false, false, humor_enabled()));
    CHECK(mutation_check_hint("add").find("dose list") != std::string::npos);
    CHECK(mutation_check_hint("lab").find("lab list") != std::string::npos);
    CHECK(failure_human_line(Language::En, FailureKind::FinalFallback)
              .find("cries") != std::string::npos);
    NetworkFailure nf("TUNNEL_INTERRUPTED", "reset", FailureKind::TunnelInterrupted);
    CHECK(nf.exit_code() == ExitCode::Network);
    return test_result();
}
