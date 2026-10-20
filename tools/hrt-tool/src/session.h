#pragma once

#include "vault.h"

#include <functional>
#include <istream>
#include <memory>
#include <ostream>
#include <string>

struct GlobalOptions;
class VaultLock;

struct SessionContext {
    VaultSession session;
    std::string master;
    bool loaded = false;
    std::function<void(const std::string &)> progress;
};

/* Owner-or-view over a vault session. When a SessionContext is supplied, the
   view points into it, so a command's mutations stay visible to the caller
   without re-deriving the vault key. */
class UnlockedVault {
public:
    UnlockedVault();
    ~UnlockedVault();
    UnlockedVault(UnlockedVault &&other) noexcept;
    UnlockedVault &operator=(UnlockedVault &&other) noexcept;
    UnlockedVault(const UnlockedVault &) = delete;
    UnlockedVault &operator=(const UnlockedVault &) = delete;

    VaultSession &session();
    const VaultSession &session() const;
    std::string &master();
    VaultLock *lock();

private:
    friend UnlockedVault unlock_full(const GlobalOptions &, std::istream &,
                                     std::ostream &, SessionContext *);
    friend UnlockedVault unlock_required(const GlobalOptions &, std::istream &,
                                         std::ostream &, SessionContext *);

    static UnlockedVault unlock_impl(const GlobalOptions &options, std::istream &in,
                                     std::ostream &out, SessionContext *context,
                                     bool acquire_lock);

    VaultSession *session_ = nullptr;
    std::string *master_ = nullptr;
    std::unique_ptr<VaultSession> owned_session_;
    std::unique_ptr<std::string> owned_master_;
    std::unique_ptr<VaultLock> lock_;
};

UnlockedVault unlock_full(const GlobalOptions &options, std::istream &in,
                          std::ostream &out, SessionContext *context = nullptr);
UnlockedVault unlock_required(const GlobalOptions &options, std::istream &in,
                              std::ostream &out, SessionContext *context = nullptr);
