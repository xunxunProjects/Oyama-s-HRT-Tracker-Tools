#include "link.h"

#include "error.h"

#include <chrono>
#include <sstream>

namespace {

int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void copy_view_to_link(const VaultData &data, LinkData &link) {
    link.url = data.server;
    link.proxy = data.proxy;
    link.mode = data.mode;
    link.token = data.token;
    link.user_id = data.user_id;
    link.username = data.username;
    link.device_pass = data.device_pass;
    link.cloud_key = data.cloud_key;
    link.device_key_pem = data.device_key_pem;
    link.device_cert_pem = data.device_cert_pem;
    link.device_fingerprint = data.device_fingerprint;
    link.last_login_failed_at = data.last_login_failed_at;
}

void copy_link_to_view(const LinkData &link, VaultData &data) {
    data.server = link.url;
    data.proxy = link.proxy;
    data.mode = link.mode;
    data.token = link.token;
    data.user_id = link.user_id;
    data.username = link.username;
    data.device_pass = link.device_pass;
    data.cloud_key = link.cloud_key;
    data.device_key_pem = link.device_key_pem;
    data.device_cert_pem = link.device_cert_pem;
    data.device_fingerprint = link.device_fingerprint;
    data.last_login_failed_at = link.last_login_failed_at;
}

void check_link_name(const std::string &name) {
    if (name.empty()) {
        throw ToolError(ExitCode::Usage, "LINK_NAME",
                        "a link name must not be empty; use config set server for the base link");
    }
    if (name.find_first_of(" \t\r\n/") != std::string::npos) {
        throw ToolError(ExitCode::Usage, "LINK_NAME",
                        "a link name must not contain whitespace or '/'");
    }
}

} // namespace

LinkData *find_link(VaultData &data, const std::string &name) {
    const auto found = data.links.find(name);
    return found == data.links.end() ? nullptr : &found->second;
}

const LinkData *find_link(const VaultData &data, const std::string &name) {
    const auto found = data.links.find(name);
    return found == data.links.end() ? nullptr : &found->second;
}

LinkData &require_link(VaultData &data, const std::string &name) {
    LinkData *link = find_link(data, name);
    if (link == nullptr) {
        throw ToolError(ExitCode::Config, "LINK_NOT_FOUND",
                        "no link named '" + name + "'");
    }
    return *link;
}

void save_active_view(VaultData &data) {
    if (data.links.count(data.active_link) == 0) {
        data.links[data.active_link] = LinkData{};
    }
    copy_view_to_link(data, data.links[data.active_link]);
}

void load_active_view(VaultData &data) {
    const LinkData &link = require_link(data, data.active_link);
    copy_link_to_view(link, data);
}

void select_link(VaultData &data, const std::string &name) {
    if (data.links.count(name) == 0) {
        throw ToolError(ExitCode::Config, "LINK_NOT_FOUND",
                        "no link named '" + name + "'");
    }
    save_active_view(data);
    data.active_link = name;
    load_active_view(data);
    data.updated_at = now_seconds();
}

void add_link(VaultData &data, const std::string &name, const LinkData &link, bool force) {
    check_link_name(name);
    if (data.links.count(name) != 0 && !force) {
        throw ToolError(ExitCode::Conflict, "LINK_EXISTS",
                        "a link named '" + name + "' already exists; pass --force to replace it");
    }
    data.links[name] = link;
    data.updated_at = now_seconds();
}

void remove_link(VaultData &data, const std::string &name, bool force) {
    if (name.empty()) {
        throw ToolError(ExitCode::Config, "LINK_BASE",
                        "the base link cannot be removed; use config unset server");
    }
    if (data.links.count(name) == 0) {
        throw ToolError(ExitCode::Config, "LINK_NOT_FOUND",
                        "no link named '" + name + "'");
    }
    if (name == data.active_link) {
        if (!force) {
            throw ToolError(ExitCode::Conflict, "LINK_ACTIVE",
                            "link '" + name + "' is active; pass --force to remove it");
        }
        select_link(data, "");
    }
    data.links.erase(name);
    data.updated_at = now_seconds();
}

void rename_link(VaultData &data, const std::string &old_name,
                 const std::string &new_name) {
    check_link_name(new_name);
    if (old_name.empty()) {
        throw ToolError(ExitCode::Config, "LINK_BASE",
                        "the base link cannot be renamed");
    }
    if (data.links.count(old_name) == 0) {
        throw ToolError(ExitCode::Config, "LINK_NOT_FOUND",
                        "no link named '" + old_name + "'");
    }
    if (data.links.count(new_name) != 0) {
        throw ToolError(ExitCode::Conflict, "LINK_EXISTS",
                        "a link named '" + new_name + "' already exists");
    }
    LinkData link = data.links[old_name];
    data.links.erase(old_name);
    data.links[new_name] = std::move(link);
    if (data.active_link == old_name) {
        data.active_link = new_name;
    }
    data.updated_at = now_seconds();
}

std::string link_summary(const std::string &name, const LinkData &link) {
    std::ostringstream out;
    out << "link: " << (name.empty() ? "(base)" : name) << "\n";
    out << "url: " << link.url << "\n";
    out << "proxy: " << (link.proxy.empty() ? "(none)" : link.proxy) << "\n";
    out << "mode: " << (link.mode.empty() ? "(default)" : link.mode) << "\n";
    out << "username: " << (link.username.empty() ? "(not logged in)" : link.username) << "\n";
    out << "token: " << (link.token.empty() ? "empty" : "set") << "\n";
    out << "device key: " << (link.device_key_pem.empty() ? "absent" : "present") << "\n";
    return out.str();
}
