#include "f2f_bridge.h"

#include "error.h"

#include "libhrtf2f.h"

#include <cstddef>
#include <string>
#include <vector>

int run_f2f(const std::vector<std::string> &args, std::ostream &out,
            std::ostream &err) {
    std::vector<char *> argv;
    argv.reserve(args.size());
    for (const std::string &arg : args) {
        argv.push_back(const_cast<char *>(arg.c_str()));
    }

    char *out_buf = nullptr;
    char *err_buf = nullptr;
    size_t out_len = 0;
    size_t err_len = 0;
    const int code = hrtf2f_run_capture(
        static_cast<int>(argv.size()), argv.data(), &out_buf, &out_len,
        &err_buf, &err_len);
    if (out_buf != nullptr) {
        out.write(out_buf, static_cast<std::streamsize>(out_len));
    }
    if (err_buf != nullptr) {
        err.write(err_buf, static_cast<std::streamsize>(err_len));
    }
    if (out_buf != nullptr) {
        hrtf2f_free(out_buf);
    }
    if (err_buf != nullptr) {
        hrtf2f_free(err_buf);
    }

    if (code == 0) {
        return 0;
    }
    if (code == 2) {
        return static_cast<int>(ExitCode::Usage);
    }
    return static_cast<int>(ExitCode::F2F);
}

std::string f2f_version() {
    char *value = hrtf2f_version();
    std::string result = value != nullptr ? value : "";
    if (value != nullptr) {
        hrtf2f_free(value);
    }
    return result;
}
