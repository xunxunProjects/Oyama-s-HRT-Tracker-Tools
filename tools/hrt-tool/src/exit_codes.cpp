#include "error.h"

const char *exit_code_name(ExitCode code) {
    switch (code) {
    case ExitCode::Ok:
        return "OK";
    case ExitCode::Internal:
        return "INTERNAL";
    case ExitCode::Usage:
        return "USAGE";
    case ExitCode::Vault:
        return "VAULT";
    case ExitCode::Network:
        return "NETWORK";
    case ExitCode::Data:
        return "DATA";
    case ExitCode::NotImplemented:
        return "NOT_IMPLEMENTED";
    case ExitCode::Auth:
        return "AUTH";
    case ExitCode::Trust:
        return "TRUST";
    case ExitCode::Permission:
        return "PERMISSION";
    case ExitCode::Conflict:
        return "CONFLICT";
    case ExitCode::Cancelled:
        return "CANCELLED";
    case ExitCode::Config:
        return "CONFIG";
    case ExitCode::F2F:
        return "F2F";
    case ExitCode::Platform:
        return "PLATFORM";
    }
    return "UNKNOWN";
}

const char *exit_code_description(ExitCode code) {
    switch (code) {
    case ExitCode::Ok:
        return "success";
    case ExitCode::Internal:
        return "unexpected internal error";
    case ExitCode::Usage:
        return "invalid command line or input value";
    case ExitCode::Vault:
        return "local vault access or state error";
    case ExitCode::Network:
        return "transport or remote failure";
    case ExitCode::Data:
        return "malformed or inconsistent data";
    case ExitCode::NotImplemented:
        return "reserved or unimplemented feature";
    case ExitCode::Auth:
        return "server authentication or authorization failure";
    case ExitCode::Trust:
        return "security-policy refusal";
    case ExitCode::Permission:
        return "local file permission or overwrite refusal";
    case ExitCode::Conflict:
        return "concurrent modification or duplicate";
    case ExitCode::Cancelled:
        return "user declined or cancelled";
    case ExitCode::Config:
        return "configuration key or value error";
    case ExitCode::F2F:
        return "F2F library error";
    case ExitCode::Platform:
        return "feature unavailable in this build";
    }
    return "unknown exit code";
}
