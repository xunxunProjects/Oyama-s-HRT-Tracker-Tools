#pragma once

#include <istream>
#include <ostream>
#include <string>
#include <vector>

struct GlobalOptions {
    std::string server = "https://hrt.mahiro.uk";
    bool server_explicit = false;
    bool require_hrt = false;
    std::string device_name;
    std::string proxy;
    bool proxy_explicit = false;
    std::string config_dir;
    std::string token_file;
    bool debug = false;
    bool trace = false;
    bool json = false;
    bool no_color = false;
    bool yes = false;
    bool force = false;
    bool probe = false;
    bool no_humor = false;
    long timeout_s = 30;
    bool trust_server = false;
    bool insecure_tls = false;
    std::string transport = "auto";
    bool cache_cloud_key = true;
    std::string compute = "local";
    std::string vault_password_file;
    bool vault_password_stdin = false;
    std::string log_file;
    std::string user_id;
    std::string account_password_file;
    bool account_password_stdin = false;
    std::string snapshot;
    std::string mode;
    bool utc = false;
};

struct ParsedArgs {
    GlobalOptions options;
    std::string command;
    std::vector<std::string> command_args;
    bool help = false;
};

struct SessionContext;

int dispatch(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
             std::ostream &err, SessionContext *context = nullptr);

std::string read_vault_password(const GlobalOptions &options, std::istream &in,
                                std::ostream &out, bool confirm);

std::string read_account_password(const GlobalOptions &options, std::istream &in,
                                  std::ostream &out);
