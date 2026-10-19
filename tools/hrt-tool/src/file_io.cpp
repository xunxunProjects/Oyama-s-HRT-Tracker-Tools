#include "file_io.h"

#include "error.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <system_error>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

[[noreturn]] void throw_limit(size_t max_bytes, const std::string &what) {
    throw ToolLimitError(what + " exceeds the " +
                         std::to_string(max_bytes / (1024 * 1024)) + " MiB limit");
}

[[noreturn]] void throw_read(const std::string &what, const std::string &detail) {
    throw ToolError(ExitCode::Vault, "FILE_READ",
                    "cannot read the " + what + ": " + detail);
}

[[noreturn]] void throw_type(const std::string &what) {
    throw ToolError(ExitCode::Data, "FILE_TYPE",
                    what + " is not a regular file");
}

} // namespace

std::string read_text_file_capped(const std::filesystem::path &path, size_t max_bytes,
                                  const std::string &what) {
#ifdef _WIN32
    std::error_code size_error;
    const auto size = std::filesystem::file_size(path, size_error);
    if (size_error) {
        throw_read(what, size_error.message());
    }
    if (size > max_bytes) {
        throw_limit(max_bytes, what);
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw_read(what, "cannot open");
    }
    std::string out;
    out.resize(max_bytes + 1);
    input.read(out.data(), static_cast<std::streamsize>(out.size()));
    const std::streamsize got = input.gcount();
    if (got < 0) {
        throw_read(what, "read failed");
    }
    out.resize(static_cast<size_t>(got));
    if (out.size() > max_bytes) {
        throw_limit(max_bytes, what);
    }
    return out;
#else
    /* O_NONBLOCK keeps open(2) from blocking on a FIFO before fstat can
       reject it; it has no effect on regular-file reads. */
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        throw_read(what, std::strerror(errno));
    }
    struct stat info {};
    if (::fstat(fd, &info) != 0) {
        const std::string detail = std::strerror(errno);
        ::close(fd);
        throw_read(what, detail);
    }
    if (!S_ISREG(info.st_mode)) {
        ::close(fd);
        throw_type(what);
    }
    std::string out;
    out.reserve(max_bytes + 1 < static_cast<size_t>(info.st_size)
                    ? max_bytes + 1
                    : static_cast<size_t>(info.st_size));
    char buffer[65536];
    while (true) {
        const size_t room = max_bytes + 1 - out.size();
        if (room == 0) {
            break;
        }
        const size_t want = room < sizeof(buffer) ? room : sizeof(buffer);
        const ssize_t count = ::read(fd, buffer, want);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            const std::string detail = std::strerror(errno);
            ::close(fd);
            throw_read(what, detail);
        }
        if (count == 0) {
            break;
        }
        out.append(buffer, static_cast<size_t>(count));
    }
    ::close(fd);
    if (out.size() > max_bytes) {
        throw_limit(max_bytes, what);
    }
    return out;
#endif
}

std::vector<uint8_t> read_binary_file_capped(const std::filesystem::path &path,
                                             size_t max_bytes, const std::string &what) {
    const std::string text = read_text_file_capped(path, max_bytes, what);
    return std::vector<uint8_t>(text.begin(), text.end());
}
