#pragma once

#include "vault.h"

#include <string>

LinkData *find_link(VaultData &data, const std::string &name);
const LinkData *find_link(const VaultData &data, const std::string &name);
LinkData &require_link(VaultData &data, const std::string &name);

/* Copy the active top-level view into links[active_link] and back. */
void save_active_view(VaultData &data);
void load_active_view(VaultData &data);

/* Save the current view, switch active_link, and load the target view. */
void select_link(VaultData &data, const std::string &name);

void add_link(VaultData &data, const std::string &name, const LinkData &link, bool force);
void remove_link(VaultData &data, const std::string &name, bool force);
void rename_link(VaultData &data, const std::string &old_name, const std::string &new_name);

/* Human-readable, redacted summary. Never includes a secret value. */
std::string link_summary(const std::string &name, const LinkData &link);
