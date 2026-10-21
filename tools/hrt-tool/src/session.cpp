#include "session.h"

#include "commands.h"
#include "error.h"
#include "net_error.h"
#include "paths.h"

#include <openssl/crypto.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

/* Serialises vault read-modify-write across processes, like SnapshotLock. */
class VaultLock {
public:
    explicit VaultLock(const std::filesystem::path &vault) {
#ifndef _WIN32
        path_ = vault;
        path_ += ".lock";
        fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd_ < 0) {
            throw ToolError(ExitCode::Permission, "VAULT_LOCK",
                            "cannot open the vault lock: " +
                                std::string(std::strerror(errno)));
        }
        while (::flock(fd_, LOCK_EX) != 0) {
            if (errno == EINTR) {
                continue;
            }
            const std::string detail = std::strerror(errno);
            ::close(fd_);
            fd_ = -1;
            throw ToolError(ExitCode::Permission, "VAULT_LOCK",
                            "cannot lock the vault: " + detail);
        }
#else
        (void)vault;
#endif
    }
    ~VaultLock() {
#ifndef _WIN32
        if (fd_ >= 0) {
            ::flock(fd_, LOCK_UN);
            ::close(fd_);
        }
#endif
    }
    VaultLock(const VaultLock &) = delete;
    VaultLock &operator=(const VaultLock &) = delete;

private:
#ifndef _WIN32
    std::filesystem::path path_;
    int fd_ = -1;
#endif
};

SessionContext::~SessionContext() {
    if (!master.empty()) {
        OPENSSL_cleanse(master.data(), master.size());
        master.clear();
    }
}

UnlockedVault::UnlockedVault() = default;
UnlockedVault::~UnlockedVault() {
    if (owned_master_ != nullptr) {
        OPENSSL_cleanse(owned_master_->data(), owned_master_->size());
    }
}

UnlockedVault::UnlockedVault(UnlockedVault &&other) noexcept
    : session_(other.session_),
      master_(other.master_),
      owned_session_(std::move(other.owned_session_)),
      owned_master_(std::move(other.owned_master_)),
      lock_(std::move(other.lock_)) {
    other.session_ = nullptr;
    other.master_ = nullptr;
}

UnlockedVault &UnlockedVault::operator=(UnlockedVault &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (owned_master_ != nullptr) {
        OPENSSL_cleanse(owned_master_->data(), owned_master_->size());
    }
    session_ = other.session_;
    master_ = other.master_;
    owned_session_ = std::move(other.owned_session_);
    owned_master_ = std::move(other.owned_master_);
    lock_ = std::move(other.lock_);
    other.session_ = nullptr;
    other.master_ = nullptr;
    return *this;
}

VaultSession &UnlockedVault::session() {
    if (session_ == nullptr) {
        throw ToolError(ExitCode::Internal, "SESSION", "vault session is not loaded");
    }
    return *session_;
}

const VaultSession &UnlockedVault::session() const {
    if (session_ == nullptr) {
        throw ToolError(ExitCode::Internal, "SESSION", "vault session is not loaded");
    }
    return *session_;
}

std::string &UnlockedVault::master() {
    if (master_ == nullptr) {
        throw ToolError(ExitCode::Internal, "SESSION", "vault session is not loaded");
    }
    return *master_;
}

VaultLock *UnlockedVault::lock() { return lock_.get(); }

UnlockedVault UnlockedVault::unlock_impl(const GlobalOptions &options, std::istream &in,
                                          std::ostream &out, SessionContext *context,
                                          bool acquire_lock) {
    const Paths paths = resolve_paths(options.config_dir);
    if (!Vault::exists(paths.vault_file)) {
        throw ToolError(ExitCode::Vault, "VAULT_MISSING",
                        "vault not found; run hrt-tool init");
    }

    UnlockedVault result;
    if (context != nullptr && context->loaded) {
        /* The caller already holds an unlocked session (and may hold the
           vault lock); re-locking from a second fd would deadlock. */
        result.session_ = &context->session;
        result.master_ = &context->master;
        return result;
    }
    if (acquire_lock) {
        result.lock_ = std::make_unique<VaultLock>(paths.vault_file);
    }

    std::string master = read_vault_password(options, in, out, false);
    VaultSession session = Vault::unlock(paths.vault_file, master);
    set_humor_enabled(session.data.humor);
    if (context != nullptr) {
        context->session = std::move(session);
        context->master = std::move(master);
        context->loaded = true;
        result.session_ = &context->session;
        result.master_ = &context->master;
    } else {
        result.owned_session_ = std::make_unique<VaultSession>(std::move(session));
        result.owned_master_ = std::make_unique<std::string>(std::move(master));
        result.session_ = result.owned_session_.get();
        result.master_ = result.owned_master_.get();
    }
    return result;
}

UnlockedVault unlock_full(const GlobalOptions &options, std::istream &in,
                          std::ostream &out, SessionContext *context) {
    return UnlockedVault::unlock_impl(options, in, out, context, true);
}

UnlockedVault unlock_required(const GlobalOptions &options, std::istream &in,
                              std::ostream &out, SessionContext *context) {
    return UnlockedVault::unlock_impl(options, in, out, context, false);
}
