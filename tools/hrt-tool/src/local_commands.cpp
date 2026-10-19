#include "local_commands.h"

#include "backup_envelope.h"
#include "encrypted_file.h"
#include "entropy.h"
#include "error.h"
#include "file_io.h"
#include "hrtcore/hrtcore.h"
#include "hrtprint/hrtprint.h"
#include "model.h"
#include "pk.h"
#include "prompt.h"
#include "snapshot.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace {

using nlohmann::json;

double now_hours() {
    return std::chrono::duration<double>(
               std::chrono::system_clock::now().time_since_epoch())
               .count() /
           3600.0;
}

int64_t now_milliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::time_t epoch_from_tm(std::tm &tm, bool utc) {
#ifdef _WIN32
    return utc ? _mkgmtime(&tm) : std::mktime(&tm);
#else
    return utc ? ::timegm(&tm) : std::mktime(&tm);
#endif
}

std::string format_time(double hours, bool utc) {
    const std::time_t seconds = static_cast<std::time_t>(hours * 3600.0);
    std::tm tm{};
#ifdef _WIN32
    if (utc) {
        gmtime_s(&tm, &seconds);
    } else {
        localtime_s(&tm, &seconds);
    }
#else
    if (utc) {
        gmtime_r(&seconds, &tm);
    } else {
        localtime_r(&seconds, &tm);
    }
#endif
    char buffer[64] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &tm);
    std::string out(buffer);
    out += utc ? " UTC" : " local";
    return out;
}

std::string iso_now() {
    const std::time_t seconds = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif
    char buffer[64] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S.000Z", &tm);
    return buffer;
}

std::string format_duration(double hours) {
    if (!std::isfinite(hours) || hours < 0.0) {
        hours = 0.0;
    }
    const int total_minutes = static_cast<int>(std::floor(hours * 60.0));
    const int days = total_minutes / (24 * 60);
    const int hour_part = (total_minutes / 60) % 24;
    const int minutes = total_minutes % 60;
    std::ostringstream out;
    if (days > 0) {
        out << days << "d ";
    }
    out << hour_part << "h " << minutes << "m";
    return out.str();
}

double parse_duration_hours(const std::string &text) {
    errno = 0;
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(value) || value < 0.0 ||
        std::strlen(end) != 1) {
        throw ToolError(ExitCode::Usage, "DURATION", "invalid duration: " + text);
    }
    switch (*end) {
    case 'm':
        return value / 60.0;
    case 'h':
        return value;
    case 'd':
        return value * 24.0;
    default:
        throw ToolError(ExitCode::Usage, "DURATION",
                        "duration unit must be m, h, or d: " + text);
    }
}

bool read_digits(const std::string &text, size_t &pos, size_t count, int &value) {
    if (pos + count > text.size()) {
        return false;
    }
    value = 0;
    for (size_t i = 0; i < count; ++i) {
        const char c = text[pos + i];
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    pos += count;
    return true;
}

double parse_time_hours(const std::string &text, bool utc_requested) {
    if (text == "now") {
        return now_hours();
    }
    if (text.size() > 1 && (text.front() == '+' || text.front() == '-')) {
        const double delta = parse_duration_hours(text.substr(1));
        return now_hours() + (text.front() == '-' ? -delta : delta);
    }

    int year = 0;
    int month = 0;
    int day = 0;
    {
        int consumed = 0;
        if (std::sscanf(text.c_str(), "%dy%dm%dd%n", &year, &month, &day, &consumed) ==
                3 &&
            consumed == static_cast<int>(text.size())) {
            if (month < 1 || month > 12 || day < 1 || day > 31) {
                throw ToolError(ExitCode::Usage, "TIME", "invalid date: " + text);
            }
            std::tm tm{};
            tm.tm_year = year - 1900;
            tm.tm_mon = month - 1;
            tm.tm_mday = day;
            tm.tm_isdst = -1;
            const std::time_t epoch = epoch_from_tm(tm, utc_requested);
            if (epoch == static_cast<std::time_t>(-1) || epoch < 0) {
                throw ToolError(ExitCode::Usage, "TIME", "invalid date: " + text);
            }
            return static_cast<double>(epoch) / 3600.0;
        }
    }

    size_t pos = 0;
    if (!read_digits(text, pos, 4, year) || pos >= text.size() || text[pos] != '-' ||
        !read_digits(text, ++pos, 2, month) || pos >= text.size() || text[pos] != '-' ||
        !read_digits(text, ++pos, 2, day)) {
        throw ToolError(ExitCode::Usage, "TIME", "invalid time: " + text);
    }
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        throw ToolError(ExitCode::Usage, "TIME", "invalid time: " + text);
    }

    int hour = 0;
    int minute = 0;
    int second = 0;
    double fraction = 0.0;
    if (pos < text.size() && (text[pos] == 'T' || text[pos] == ' ')) {
        ++pos;
        if (!read_digits(text, pos, 2, hour) || pos >= text.size() || text[pos] != ':' ||
            !read_digits(text, ++pos, 2, minute)) {
            throw ToolError(ExitCode::Usage, "TIME", "invalid time: " + text);
        }
        if (pos < text.size() && text[pos] == ':') {
            ++pos;
            if (!read_digits(text, pos, 2, second)) {
                throw ToolError(ExitCode::Usage, "TIME", "invalid time: " + text);
            }
        }
        if (pos < text.size() && text[pos] == '.') {
            ++pos;
            size_t digits = 0;
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
                if (digits < 9) {
                    fraction = fraction * 10.0 + (text[pos] - '0');
                    ++digits;
                }
                ++pos;
            }
            if (digits == 0) {
                throw ToolError(ExitCode::Usage, "TIME", "invalid time: " + text);
            }
            static const double kScale[] = {0.1,       0.01,       0.001,
                                            0.0001,    0.00001,    0.000001,
                                            0.0000001, 0.00000001, 0.000000001};
            fraction *= kScale[digits - 1];
        }
        if (hour > 23 || minute > 59 || second > 59) {
            throw ToolError(ExitCode::Usage, "TIME", "invalid time: " + text);
        }
    }

    bool has_offset = false;
    int offset_seconds = 0;
    if (pos < text.size() && (text[pos] == 'Z' || text[pos] == 'z')) {
        ++pos;
        has_offset = true;
    } else if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
        const int sign = text[pos] == '-' ? -1 : 1;
        ++pos;
        int offset_hour = 0;
        int offset_minute = 0;
        if (!read_digits(text, pos, 2, offset_hour)) {
            throw ToolError(ExitCode::Usage, "TIME", "invalid time: " + text);
        }
        if (pos < text.size() && text[pos] == ':') {
            ++pos;
        }
        if (!read_digits(text, pos, 2, offset_minute) || offset_hour > 23 ||
            offset_minute > 59) {
            throw ToolError(ExitCode::Usage, "TIME", "invalid time: " + text);
        }
        offset_seconds = sign * (offset_hour * 3600 + offset_minute * 60);
        has_offset = true;
    }
    if (pos != text.size()) {
        throw ToolError(ExitCode::Usage, "TIME", "invalid time: " + text);
    }

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = -1;
    const std::time_t epoch =
        has_offset ? epoch_from_tm(tm, true) - offset_seconds
                   : epoch_from_tm(tm, utc_requested);
    if (epoch == static_cast<std::time_t>(-1) || epoch < 0) {
        throw ToolError(ExitCode::Usage, "TIME", "invalid time: " + text);
    }
    return static_cast<double>(epoch) / 3600.0 + fraction / 3600.0;
}

double parse_dose_mg(const std::string &text) {
    double value = 0.0;
    char unit[16] = {};
    const int matched = std::sscanf(text.c_str(), "%lf%15s", &value, unit);
    if (matched < 1 || !std::isfinite(value) || value <= 0.0) {
        throw ToolError(ExitCode::Usage, "DOSE", "invalid dose: " + text);
    }
    std::string suffix(unit);
    for (char &c : suffix) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (suffix.empty() || suffix == "mg") {
        return value;
    }
    if (suffix == "ug" || suffix == "µg" || suffix == "mcg") {
        return value / 1000.0;
    }
    if (suffix == "g") {
        return value * 1000.0;
    }
    throw ToolError(ExitCode::Usage, "DOSE", "unsupported dose unit: " + suffix);
}

std::string normalized_key(const std::string &text) {
    std::string out;
    for (char c : text) {
        if (c >= 'A' && c <= 'Z') {
            out.push_back(static_cast<char>(c - 'A' + 'a'));
        } else if (c >= 'a' && c <= 'z') {
            out.push_back(c);
        } else if (c >= '0' && c <= '9') {
            out.push_back(c);
        }
    }
    return out;
}

int parse_route(const std::string &text) {
    static const std::map<std::string, int> kRoutes = {
        {"sublingual", HRTCORE_ROUTE_SUBLINGUAL},
        {"sl", HRTCORE_ROUTE_SUBLINGUAL},
        {"injection", HRTCORE_ROUTE_INJECTION},
        {"inject", HRTCORE_ROUTE_INJECTION},
        {"inj", HRTCORE_ROUTE_INJECTION},
        {"patchapply", HRTCORE_ROUTE_PATCH_APPLY},
        {"apply", HRTCORE_ROUTE_PATCH_APPLY},
        {"patchremove", HRTCORE_ROUTE_PATCH_REMOVE},
        {"remove", HRTCORE_ROUTE_PATCH_REMOVE},
        {"gel", HRTCORE_ROUTE_GEL},
        {"oral", HRTCORE_ROUTE_ORAL},
        {"pill", HRTCORE_ROUTE_ORAL},
    };
    const auto found = kRoutes.find(normalized_key(text));
    if (found == kRoutes.end()) {
        throw ToolError(ExitCode::Usage, "ROUTE", "unknown route: " + text);
    }
    return found->second;
}

std::string route_display(int route) {
    switch (route) {
    case HRTCORE_ROUTE_SUBLINGUAL:
        return "sublingual";
    case HRTCORE_ROUTE_INJECTION:
        return "injection";
    case HRTCORE_ROUTE_PATCH_APPLY:
        return "patchApply";
    case HRTCORE_ROUTE_PATCH_REMOVE:
        return "patchRemove";
    case HRTCORE_ROUTE_GEL:
        return "gel";
    case HRTCORE_ROUTE_ORAL:
        return "oral";
    default:
        return "oral";
    }
}

int parse_ester(const std::string &drug) {
    static const std::map<std::string, int> kEsters = {
        {"estradiolvalerate", HRTCORE_ESTER_EV},  {"ev", HRTCORE_ESTER_EV},
        {"estradiolbenzoate", HRTCORE_ESTER_EB},  {"eb", HRTCORE_ESTER_EB},
        {"estradiolcypionate", HRTCORE_ESTER_EC}, {"ec", HRTCORE_ESTER_EC},
        {"estradiolenanthate", HRTCORE_ESTER_EN}, {"en", HRTCORE_ESTER_EN},
        {"estradiolundecylate", HRTCORE_ESTER_EU}, {"eu", HRTCORE_ESTER_EU},
        {"estradiol", HRTCORE_ESTER_E2},          {"e2", HRTCORE_ESTER_E2},
        {"cyproteroneacetate", HRTCORE_ESTER_CPA}, {"cyproterone", HRTCORE_ESTER_CPA},
        {"cpa", HRTCORE_ESTER_CPA},
        {"testosterone", HRTCORE_ESTER_T},        {"t", HRTCORE_ESTER_T},
        {"testosteronecypionate", HRTCORE_ESTER_TC}, {"tc", HRTCORE_ESTER_TC},
        {"testosteroneenanthate", HRTCORE_ESTER_TE}, {"te", HRTCORE_ESTER_TE},
        {"testosteroneundecanoate", HRTCORE_ESTER_TU}, {"tu", HRTCORE_ESTER_TU},
        {"spironolactone", HRTCORE_ESTER_SPIRO},  {"spiro", HRTCORE_ESTER_SPIRO},
        {"bicalutamide", HRTCORE_ESTER_BICA},     {"bica", HRTCORE_ESTER_BICA},
        {"medroxyprogesterone", HRTCORE_ESTER_MPA}, {"mpa", HRTCORE_ESTER_MPA},
        {"leuprorelin", HRTCORE_ESTER_LEUP},      {"leup", HRTCORE_ESTER_LEUP},
        {"triptorelin", HRTCORE_ESTER_TRIP},      {"trip", HRTCORE_ESTER_TRIP},
        {"raloxifene", HRTCORE_ESTER_RLX},        {"rlx", HRTCORE_ESTER_RLX},
    };
    const std::string key = normalized_key(drug);
    const auto found = kEsters.find(key);
    if (found == kEsters.end()) {
        throw ToolError(ExitCode::Usage, "DRUG", "unknown drug: " + drug);
    }
    return found->second;
}

std::string ester_code(int ester) {
    static const char *const kCodes[] = {
        "E2", "EB", "EV", "EC", "EN", "EU", "CPA", "SPIRO", "BICA",
        "MPA", "LEUP", "TRIP", "RLX", "T", "TC", "TE", "TU",
    };
    return ester >= 0 && ester <= HRTCORE_ESTER_TU ? kCodes[ester] : "";
}

std::string drug_display(int ester) {
    switch (ester) {
    case HRTCORE_ESTER_E2:
        return "Estradiol";
    case HRTCORE_ESTER_EB:
        return "Estradiol benzoate";
    case HRTCORE_ESTER_EV:
        return "Estradiol valerate";
    case HRTCORE_ESTER_EC:
        return "Estradiol cypionate";
    case HRTCORE_ESTER_EN:
        return "Estradiol enanthate";
    case HRTCORE_ESTER_EU:
        return "Estradiol undecylate";
    case HRTCORE_ESTER_CPA:
        return "Cyproterone acetate";
    case HRTCORE_ESTER_T:
        return "Testosterone";
    case HRTCORE_ESTER_TC:
        return "Testosterone cypionate";
    case HRTCORE_ESTER_TE:
        return "Testosterone enanthate";
    case HRTCORE_ESTER_TU:
        return "Testosterone undecanoate";
    default:
        return "Unmodelled drug";
    }
}

bool is_t_ester(int ester) {
    return ester == HRTCORE_ESTER_T || ester == HRTCORE_ESTER_TC ||
           ester == HRTCORE_ESTER_TE || ester == HRTCORE_ESTER_TU;
}

bool is_modelled_ester(int ester) {
    switch (ester) {
    case HRTCORE_ESTER_SPIRO:
    case HRTCORE_ESTER_BICA:
    case HRTCORE_ESTER_MPA:
    case HRTCORE_ESTER_LEUP:
    case HRTCORE_ESTER_TRIP:
    case HRTCORE_ESTER_RLX:
        return false;
    default:
        return true;
    }
}

int default_route(int ester) {
    if (ester == HRTCORE_ESTER_CPA) {
        return HRTCORE_ROUTE_ORAL;
    }
    if (ester == HRTCORE_ESTER_T) {
        return HRTCORE_ROUTE_GEL;
    }
    if (ester == HRTCORE_ESTER_E2) {
        return HRTCORE_ROUTE_ORAL;
    }
    if (ester >= HRTCORE_ESTER_EB && ester <= HRTCORE_ESTER_EU) {
        return HRTCORE_ROUTE_INJECTION;
    }
    if (ester == HRTCORE_ESTER_TC || ester == HRTCORE_ESTER_TE ||
        ester == HRTCORE_ESTER_TU) {
        return HRTCORE_ROUTE_INJECTION;
    }
    return HRTCORE_ROUTE_ORAL;
}

std::string random_id() {
    std::array<uint8_t, 16> bytes{};
    hrtcore_error error{};
    if (hrtcore_random_bytes(bytes.data(), bytes.size(), &error) != HRTCORE_OK) {
        throw ToolError(ExitCode::Data, "ID", error.message);
    }
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (uint8_t byte : bytes) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0x0f]);
    }
    return out;
}

double number_or(const json &object, const char *key, double fallback) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number()) {
        return fallback;
    }
    const double value = found->get<double>();
    return std::isfinite(value) ? value : fallback;
}

std::string string_or(const json &object, const char *key, const std::string &fallback) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string()) {
        return fallback;
    }
    return found->get<std::string>();
}

double parse_lab_value(const std::string &text) {
    errno = 0;
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(value) || value < 0.0 ||
        value > 1.0e9) {
        throw ToolError(ExitCode::Usage, "VALUE",
                        "lab value must be a finite number between 0 and 1e9: " + text);
    }
    return value;
}

std::string canonical_lab_unit(const std::string &text) {
    const std::string key = normalized_key(text);
    if (key == "pgml") {
        return "pg/ml";
    }
    if (key == "pmoll") {
        return "pmol/l";
    }
    if (key == "ngdl") {
        return "ng/dl";
    }
    if (key == "nmoll") {
        return "nmol/l";
    }
    throw ToolError(ExitCode::Usage, "UNIT", "unknown lab unit: " + text);
}

std::string parse_assay(const std::string &text) {
    if (text != "lcms" && text != "immuno") {
        throw ToolError(ExitCode::Usage, "ASSAY", "assay must be lcms or immuno");
    }
    return text;
}

double class_value(const PkResult &result, int ester, double hour) {
    if (is_t_ester(ester)) {
        return hrtcore_pk_interp_t(&result.raw(), hour);
    }
    if (ester == HRTCORE_ESTER_CPA) {
        return hrtcore_pk_interp_cpa(&result.raw(), hour);
    }
    return hrtcore_pk_interp_e2(&result.raw(), hour);
}

std::string format_concentration(double value, int ester) {
    std::ostringstream out;
    out << std::fixed;
    if (is_t_ester(ester)) {
        out << std::setprecision(0) << value << " ng/dL";
    } else if (ester == HRTCORE_ESTER_CPA) {
        out << std::setprecision(2) << value << " ng/mL";
    } else {
        out << std::setprecision(1) << value << " pg/mL";
    }
    return out.str();
}

std::string format_lab_value(double value, const std::string &unit) {
    std::ostringstream out;
    out << std::fixed;
    if (unit == "pg/ml") {
        out << std::setprecision(1) << value << " pg/mL (" << std::setprecision(1)
            << value * 3.671 << " pmol/L)";
    } else if (unit == "pmol/l") {
        out << std::setprecision(1) << value << " pmol/L (" << std::setprecision(1)
            << value / 3.671 << " pg/mL)";
    } else if (unit == "ng/dl") {
        out << std::setprecision(1) << value << " ng/dL (" << std::setprecision(2)
            << value / 28.842 << " nmol/L)";
    } else if (unit == "nmol/l") {
        out << std::setprecision(2) << value << " nmol/L (" << std::setprecision(1)
            << value * 28.842 << " ng/dL)";
    } else {
        out << std::setprecision(1) << value << " " << unit;
    }
    return out.str();
}

ModeKey parse_mode_option(const std::string &value) {
    return value == "transmasc" ? ModeKey::Transmasc : ModeKey::Transfem;
}

struct CommandFlags {
    std::map<std::string, std::string> values;
    std::set<std::string> flags;
    std::vector<std::string> positionals;
    std::vector<std::string> extras;
};

CommandFlags parse_command_flags(const std::vector<std::string> &args,
                                 const std::set<std::string> &value_flags,
                                 const std::set<std::string> &boolean_flags) {
    CommandFlags out;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string token = args[i];
        if (token.empty() || token.front() != '-') {
            out.positionals.push_back(token);
            continue;
        }
        std::string name = token;
        std::string value;
        bool has_value = false;
        const std::string::size_type equals = token.find('=');
        if (equals != std::string::npos) {
            name = token.substr(0, equals);
            value = token.substr(equals + 1);
            has_value = true;
        }
        if (boolean_flags.count(name) != 0) {
            out.flags.insert(name);
            continue;
        }
        if (value_flags.count(name) == 0) {
            throw ToolError(ExitCode::Usage, "FLAG", "unknown flag: " + name);
        }
        if (!has_value) {
            if (i + 1 >= args.size()) {
                throw ToolError(ExitCode::Usage, "FLAG", "missing value for " + name);
            }
            value = args[++i];
        }
        if (name == "--extras") {
            out.extras.push_back(value);
        } else {
            out.values[name] = value;
        }
    }
    return out;
}

std::string require_value(const CommandFlags &flags, const std::string &name) {
    const auto found = flags.values.find(name);
    if (found == flags.values.end() || found->second.empty()) {
        throw ToolError(ExitCode::Usage, "FLAG", "missing " + name);
    }
    return found->second;
}

/*
 * An advisory lock beside the snapshot serialises read-modify-write across
 * processes, so two concurrent `add` commands cannot both read the same base
 * and overwrite each other. The lock file holds no data.
 */
class SnapshotLock {
public:
    explicit SnapshotLock(const std::filesystem::path &snapshot) {
#ifndef _WIN32
        path_ = snapshot;
        path_ += ".lock";
        fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd_ < 0) {
            throw ToolError(ExitCode::Vault, "SNAPSHOT_LOCK",
                            "cannot open the snapshot lock: " +
                                std::string(std::strerror(errno)));
        }
        while (::flock(fd_, LOCK_EX) != 0) {
            if (errno == EINTR) {
                continue;
            }
            const std::string detail = std::strerror(errno);
            ::close(fd_);
            fd_ = -1;
            throw ToolError(ExitCode::Vault, "SNAPSHOT_LOCK",
                            "cannot lock the snapshot: " + detail);
        }
#else
        (void)snapshot;
#endif
    }
    ~SnapshotLock() {
#ifndef _WIN32
        if (fd_ >= 0) {
            ::flock(fd_, LOCK_UN);
            ::close(fd_);
        }
#endif
    }
    SnapshotLock(const SnapshotLock &) = delete;
    SnapshotLock &operator=(const SnapshotLock &) = delete;

private:
#ifndef _WIN32
    std::filesystem::path path_;
    int fd_ = -1;
#endif
};

struct LocalContext {
    std::filesystem::path path;
    json payload;
    std::string account_password;
    std::string user_id;
    std::unique_ptr<SnapshotLock> lock;
};

LocalContext open_context(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                          bool for_write) {
    if (parsed.options.snapshot.empty()) {
        throw ToolError(ExitCode::Usage, "SNAPSHOT_REQUIRED",
                        "pass --snapshot FILE; the live API lands in M4");
    }
    LocalContext context;
    context.path = parsed.options.snapshot;
    if (for_write) {
        context.lock = std::make_unique<SnapshotLock>(context.path);
    }
    if (snapshot_is_encrypted(context.path)) {
        if (parsed.options.user_id.empty()) {
            throw ToolError(ExitCode::Usage, "USER_ID_REQUIRED",
                            "pass --user-id for an encrypted snapshot");
        }
        context.user_id = parsed.options.user_id;
        context.account_password = read_account_password(parsed.options, in, out);
        context.payload = load_snapshot(context.path, context.account_password,
                                        context.user_id);
    } else {
        context.payload = load_snapshot(context.path, "", "");
    }
    return context;
}

void save_context(LocalContext &context) {
    if (context.account_password.empty() || context.user_id.empty()) {
        throw ToolError(ExitCode::Usage, "SNAPSHOT_CREDENTIALS",
                        "writing a plaintext snapshot is not supported; use an encrypted "
                        "snapshot with --user-id and an account password");
    }
    prune_tombstones(context.payload, now_milliseconds());
    save_snapshot(context.path, context.payload, context.account_password, context.user_id);
}

ModeKey resolve_mode(const ParsedArgs &parsed, const json &payload, bool required) {
    if (!parsed.options.mode.empty()) {
        return parse_mode_option(parsed.options.mode);
    }
    if (const auto declared = declared_mode(payload); declared.has_value()) {
        return *declared;
    }
    if (required) {
        throw ToolError(ExitCode::Usage, "MODE_REQUIRED",
                        "pass --mode transfem|transmasc");
    }
    return ModeKey::Transfem;
}

json *find_record(json &block, RecordKind kind, const std::string &id) {
    const char *key = record_kind_name(kind);
    if (!block.contains(key) || !block.at(key).is_array()) {
        return nullptr;
    }
    for (json &record : block[key]) {
        if (record.is_object() && record.value("id", std::string{}) == id) {
            return &record;
        }
    }
    return nullptr;
}

void render_view(const json &view, bool as_json, std::ostream &out) {
    std::vector<char> buffer(1 << 20);
    size_t length = 0;
    char error[512] = {};
    const std::string text = view.dump();
    const char *format = as_json ? "json" : "table";
    int code = hrtprint_render(text.c_str(), format, buffer.data(), buffer.size(),
                               &length, error, sizeof(error));
    if (code == 2) {
        buffer.resize(16 * 1024 * 1024);
        length = 0;
        code = hrtprint_render(text.c_str(), format, buffer.data(), buffer.size(),
                               &length, error, sizeof(error));
    }
    if (code != 0) {
        throw ToolError(ExitCode::Data, "PRINT", std::string(error));
    }
    out.write(buffer.data(), static_cast<std::streamsize>(length));
}

std::vector<std::pair<std::string, double>> parse_offsets(const CommandFlags &flags) {
    if (flags.values.count("--at") != 0) {
        std::vector<std::pair<std::string, double>> offsets;
        std::istringstream stream(flags.values.at("--at"));
        std::string item;
        while (std::getline(stream, item, ',')) {
            if (item.empty()) {
                continue;
            }
            offsets.emplace_back(item, parse_duration_hours(item));
        }
        if (offsets.empty()) {
            throw ToolError(ExitCode::Usage, "AT", "--at needs at least one offset");
        }
        return offsets;
    }
    return {{"1m", 1.0 / 60.0}, {"5m", 5.0 / 60.0}, {"10m", 10.0 / 60.0},
            {"30m", 0.5},       {"1h", 1.0},        {"2h", 2.0},
            {"4h", 4.0},        {"8h", 8.0},        {"12h", 12.0},
            {"24h", 24.0},      {"2d", 48.0}};
}

int handle_look(const ParsedArgs &parsed, std::istream &in, std::ostream &out) {
    LocalContext context = open_context(parsed, in, out, false);
    const json payload = normalize_sync_payload(context.payload);
    const ModeKey mode = resolve_mode(parsed, payload, false);
    const std::string mode_name = mode_key_name(mode);
    if (!payload.at("modes").contains(mode_name) ||
        !payload.at("modes").at(mode_name).is_object()) {
        throw ToolError(ExitCode::Data, "MODE", "snapshot has no mode block");
    }
    const json &block = payload.at("modes").at(mode_name);
    const CommandFlags flags =
        parse_command_flags(parsed.command_args, {"--at", "--labs"}, {"--all", "-a"});

    const double current = now_hours();
    const double weight =
        payload.contains("weight") && payload.at("weight").is_number()
            ? payload.at("weight").get<double>()
            : 70.0;
    const json params_json =
        payload.contains("pkParams") ? payload.at("pkParams") : json();
    const hrtcore_pk_params params = sanitize_pk_params(params_json);

    std::vector<PkEvent> events;
    if (block.contains("events") && block.at("events").is_array()) {
        for (const json &event : block.at("events")) {
            events.push_back(pk_event_from_json(event));
        }
    }
    std::optional<PkResult> simulation = run_pk(events, weight, params, current);

    json view;
    view["now"] = format_time(current, parsed.options.utc);
    view["mode"] = mode_name;
    view["weightKg"] = weight;
    view["compounds"] = json::array();

    if (simulation.has_value()) {
        struct Group {
            int route;
            int ester;
            double last;
        };
        std::map<std::pair<int, int>, Group> groups;
        for (const PkEvent &event : events) {
            if (!is_modelled_ester(event.ester) ||
                event.route == HRTCORE_ROUTE_PATCH_REMOVE) {
                continue;
            }
            const std::pair<int, int> key{event.route, event.ester};
            const auto found = groups.find(key);
            if (found == groups.end()) {
                groups[key] = Group{event.route, event.ester, event.time_h};
            } else if (event.time_h > found->second.last) {
                found->second.last = event.time_h;
            }
        }
        const auto offsets = parse_offsets(flags);
        for (const auto &[key, group] : groups) {
            json compound;
            compound["drug"] = drug_display(group.ester);
            compound["route"] = route_display(group.route);
            compound["lastDose"] = format_duration(current - group.last);
            compound["now"] =
                format_concentration(class_value(*simulation, group.ester, current),
                                     group.ester);
            compound["offsets"] = json::array();
            for (const auto &[label, hours] : offsets) {
                compound["offsets"].push_back(
                    {{"label", label},
                     {"value", format_concentration(
                                   class_value(*simulation, group.ester, current + hours),
                                   group.ester)}});
            }
            view["compounds"].push_back(compound);
        }
    }

    view["labs"] = json::array();
    std::vector<json> labs;
    if (block.contains("labResults") && block.at("labResults").is_array()) {
        for (const json &lab : block.at("labResults")) {
            labs.push_back(lab);
        }
    }
    std::sort(labs.begin(), labs.end(), [](const json &a, const json &b) {
        return number_or(a, "timeH", 0.0) > number_or(b, "timeH", 0.0);
    });
    int lab_count = 3;
    if (flags.values.count("--labs") != 0) {
        lab_count = std::max(0, std::atoi(flags.values.at("--labs").c_str()));
    }
    for (int i = 0; i < lab_count && i < static_cast<int>(labs.size()); ++i) {
        const json &lab = labs[static_cast<size_t>(i)];
        const std::string unit = string_or(lab, "unit", "");
        const std::string assay = string_or(lab, "assay", "");
        const bool t_unit = unit == "ng/dl" || unit == "nmol/l";
        json view_lab;
        view_lab["time"] =
            format_time(number_or(lab, "timeH", 0.0), parsed.options.utc);
        view_lab["name"] = t_unit ? "Testosterone" : "Estradiol";
        view_lab["value"] = format_lab_value(number_or(lab, "concValue", 0.0), unit);
        view_lab["method"] =
            assay == "lcms" ? "LC-MS/MS" : (assay == "immuno" ? "Immunoassay" : "");
        view["labs"].push_back(view_lab);
    }

    view["warnings"] = json::array();
    if (!simulation.has_value()) {
        view["warnings"].push_back("warning: no modelled doses");
    }
    view["warnings"].push_back(
        "warning: uncalibrated baseline (calibration is not ported yet)");
    render_view(view, parsed.options.json, out);
    return 0;
}

int handle_add(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
               std::ostream &err) {
    LocalContext context = open_context(parsed, in, out, true);
    const CommandFlags flags = parse_command_flags(
        parsed.command_args, {"--drug", "--time", "--measurement", "--dose", "--route"}, {});
    const std::string drug = require_value(flags, "--drug");
    const std::string time_text = require_value(flags, "--time");
    const std::string dose_text = flags.values.count("--measurement") != 0
                                      ? flags.values.at("--measurement")
                                      : flags.values.count("--dose") != 0
                                            ? flags.values.at("--dose")
                                            : std::string{};
    if (dose_text.empty()) {
        throw ToolError(ExitCode::Usage, "DOSE", "pass --measurement (for example 2mg)");
    }
    const int ester = parse_ester(drug);
    const double dose = parse_dose_mg(dose_text);
    const double time_h = parse_time_hours(time_text, parsed.options.utc);
    int route = default_route(ester);
    if (flags.values.count("--route") != 0) {
        route = parse_route(flags.values.at("--route"));
    } else {
        err << "assuming route: " << route_display(route) << "\n";
    }

    json payload = normalize_sync_payload(context.payload);
    const ModeKey mode = resolve_mode(parsed, payload, true);
    json &block = mode_block(payload, mode);
    if (!block.contains("events") || !block.at("events").is_array()) {
        block["events"] = json::array();
    }

    json event{
        {"id", random_id()},
        {"route", route_display(route)},
        {"ester", ester_code(ester)},
        {"doseMG", dose},
        {"timeH", time_h},
        {"extras", json::object()},
        {"updatedAt", now_milliseconds()},
    };
    for (const std::string &extra : flags.extras) {
        const std::string::size_type equals = extra.find('=');
        if (equals == std::string::npos) {
            throw ToolError(ExitCode::Usage, "EXTRAS",
                            "--extras must be key=value: " + extra);
        }
        const std::string key = extra.substr(0, equals);
        const std::string value = extra.substr(equals + 1);
        static const std::set<std::string> kKeys = {
            "concentrationMGmL",   "areaCM2",       "releaseRateUGPerDay",
            "sublingualTheta",     "sublingualTier", "gelSite",
            "patchWearH",
        };
        if (kKeys.count(key) == 0) {
            throw ToolError(ExitCode::Usage, "EXTRAS", "unknown extra: " + key);
        }
        char *end = nullptr;
        const double number = std::strtod(value.c_str(), &end);
        if (end == value.c_str() || *end != '\0' || !std::isfinite(number)) {
            throw ToolError(ExitCode::Usage, "EXTRAS",
                            "extra value must be a number: " + extra);
        }
        event["extras"][key] = number;
    }
    block["events"].push_back(event);
    payload["meta"]["version"] = 2;
    context.payload = payload;
    save_context(context);
    out << "added " << event.at("id").get<std::string>() << "\n";
    return 0;
}

int handle_dose(const ParsedArgs &parsed, std::istream &in, std::ostream &out) {
    const CommandFlags flags = parse_command_flags(
        parsed.command_args,
        {"--id", "--drug", "--route", "--time", "--measurement", "--dose"}, {});
    if (flags.positionals.empty()) {
        throw ToolError(ExitCode::Usage, "USAGE", "dose needs list, edit, or delete");
    }
    const std::string sub = flags.positionals.front();
    LocalContext context = open_context(parsed, in, out, sub != "list");
    json payload = normalize_sync_payload(context.payload);
    const ModeKey mode = resolve_mode(parsed, payload, sub != "list");
    json &block = mode_block(payload, mode);
    if (!block.contains("events") || !block.at("events").is_array()) {
        block["events"] = json::array();
    }

    if (sub == "list") {
        json events = block.at("events");
        std::sort(events.begin(), events.end(), [](const json &a, const json &b) {
            return number_or(a, "timeH", 0.0) > number_or(b, "timeH", 0.0);
        });
        if (parsed.options.json) {
            out << events.dump(2) << "\n";
            return 0;
        }
        for (const json &event : events) {
            const int ester = ester_from_string(string_or(event, "ester", ""));
            out << string_or(event, "id", "") << "  "
                << format_time(number_or(event, "timeH", 0.0), parsed.options.utc) << "  "
                << drug_display(ester) << "  " << string_or(event, "route", "") << "  "
                << number_or(event, "doseMG", 0.0) << " mg\n";
        }
        return 0;
    }
    if (sub == "delete") {
        const std::string id = require_value(flags, "--id");
        write_tombstone(payload, mode, RecordKind::Events, id, now_milliseconds());
        context.payload = payload;
        save_context(context);
        out << "deleted " << id << "\n";
        return 0;
    }
    if (sub == "edit") {
        const std::string id = require_value(flags, "--id");
        json *record = find_record(block, RecordKind::Events, id);
        if (record == nullptr) {
            throw ToolError(ExitCode::Data, "NOT_FOUND", "no event with id " + id);
        }
        if (flags.values.count("--drug") != 0) {
            (*record)["ester"] = ester_code(parse_ester(flags.values.at("--drug")));
        }
        if (flags.values.count("--route") != 0) {
            (*record)["route"] = route_display(parse_route(flags.values.at("--route")));
        }
        if (flags.values.count("--time") != 0) {
            (*record)["timeH"] =
                parse_time_hours(flags.values.at("--time"), parsed.options.utc);
        }
        if (flags.values.count("--measurement") != 0) {
            (*record)["doseMG"] = parse_dose_mg(flags.values.at("--measurement"));
        } else if (flags.values.count("--dose") != 0) {
            (*record)["doseMG"] = parse_dose_mg(flags.values.at("--dose"));
        }
        (*record)["updatedAt"] = now_milliseconds();
        clear_tombstone(payload, mode, RecordKind::Events, id);
        context.payload = payload;
        save_context(context);
        out << "updated " << id << "\n";
        return 0;
    }
    throw ToolError(ExitCode::Usage, "USAGE", "dose needs list, edit, or delete");
}

int handle_lab(const ParsedArgs &parsed, std::istream &in, std::ostream &out) {
    const CommandFlags flags = parse_command_flags(
        parsed.command_args, {"--id", "--time", "--value", "--unit", "--assay"}, {});
    if (flags.positionals.empty()) {
        throw ToolError(ExitCode::Usage, "USAGE", "lab needs list, add, edit, or delete");
    }
    const std::string sub = flags.positionals.front();
    LocalContext context = open_context(parsed, in, out, sub != "list");
    json payload = normalize_sync_payload(context.payload);
    const ModeKey mode = resolve_mode(parsed, payload, sub != "list");
    json &block = mode_block(payload, mode);
    if (!block.contains("labResults") || !block.at("labResults").is_array()) {
        block["labResults"] = json::array();
    }

    if (sub == "list") {
        json labs = block.at("labResults");
        std::sort(labs.begin(), labs.end(), [](const json &a, const json &b) {
            return number_or(a, "timeH", 0.0) > number_or(b, "timeH", 0.0);
        });
        if (parsed.options.json) {
            out << labs.dump(2) << "\n";
            return 0;
        }
        for (const json &lab : labs) {
            out << string_or(lab, "id", "") << "  "
                << format_time(number_or(lab, "timeH", 0.0), parsed.options.utc) << "  "
                << number_or(lab, "concValue", 0.0) << " "
                << string_or(lab, "unit", "") << "  "
                << string_or(lab, "assay", "") << "\n";
        }
        return 0;
    }
    if (sub == "add") {
        json lab{
            {"id", random_id()},
            {"timeH", parse_time_hours(require_value(flags, "--time"), parsed.options.utc)},
            {"concValue", parse_lab_value(require_value(flags, "--value"))},
            {"unit", canonical_lab_unit(require_value(flags, "--unit"))},
            {"updatedAt", now_milliseconds()},
        };
        if (flags.values.count("--assay") != 0) {
            lab["assay"] = parse_assay(flags.values.at("--assay"));
        }
        block["labResults"].push_back(lab);
        payload["meta"]["version"] = 2;
        context.payload = payload;
        save_context(context);
        out << "added " << lab.at("id").get<std::string>() << "\n";
        return 0;
    }
    if (sub == "delete") {
        const std::string id = require_value(flags, "--id");
        write_tombstone(payload, mode, RecordKind::LabResults, id, now_milliseconds());
        context.payload = payload;
        save_context(context);
        out << "deleted " << id << "\n";
        return 0;
    }
    if (sub == "edit") {
        const std::string id = require_value(flags, "--id");
        json *record = find_record(block, RecordKind::LabResults, id);
        if (record == nullptr) {
            throw ToolError(ExitCode::Data, "NOT_FOUND", "no lab with id " + id);
        }
        if (flags.values.count("--time") != 0) {
            (*record)["timeH"] =
                parse_time_hours(flags.values.at("--time"), parsed.options.utc);
        }
        if (flags.values.count("--value") != 0) {
            (*record)["concValue"] = parse_lab_value(flags.values.at("--value"));
        }
        if (flags.values.count("--unit") != 0) {
            (*record)["unit"] = canonical_lab_unit(flags.values.at("--unit"));
        }
        if (flags.values.count("--assay") != 0) {
            (*record)["assay"] = parse_assay(flags.values.at("--assay"));
        }
        (*record)["updatedAt"] = now_milliseconds();
        clear_tombstone(payload, mode, RecordKind::LabResults, id);
        context.payload = payload;
        save_context(context);
        out << "updated " << id << "\n";
        return 0;
    }
    throw ToolError(ExitCode::Usage, "USAGE", "lab needs list, add, edit, or delete");
}

int handle_weight(const ParsedArgs &parsed, std::istream &in, std::ostream &out) {
    const CommandFlags flags =
        parse_command_flags(parsed.command_args, {"--kg"}, {});
    if (flags.positionals.empty()) {
        throw ToolError(ExitCode::Usage, "USAGE", "weight needs get or set");
    }
    const std::string sub = flags.positionals.front();
    LocalContext context = open_context(parsed, in, out, sub == "set");
    json payload = normalize_sync_payload(context.payload);
    if (sub == "get") {
        if (payload.contains("weight") && payload.at("weight").is_number()) {
            out << "weight: " << payload.at("weight").get<double>() << " kg\n";
        } else {
            out << "weight: unstated\n";
        }
        return 0;
    }
    if (sub == "set") {
        const double kg = std::atof(require_value(flags, "--kg").c_str());
        if (!std::isfinite(kg) || kg < 20.0 || kg > 400.0) {
            throw ToolError(ExitCode::Usage, "WEIGHT",
                            "weight must be between 20 and 400 kg");
        }
        payload["weight"] = kg;
        payload["weightUpdatedAt"] = now_milliseconds();
        payload["meta"]["version"] = 2;
        context.payload = payload;
        save_context(context);
        out << "weight set: " << kg << " kg\n";
        return 0;
    }
    throw ToolError(ExitCode::Usage, "USAGE", "weight needs get or set");
}

int handle_export(const ParsedArgs &parsed, std::istream &in, std::ostream &out) {
    const CommandFlags flags =
        parse_command_flags(parsed.command_args, {"--out", "--format"}, {});
    const std::string out_path = require_value(flags, "--out");
    const std::string format =
        flags.values.count("--format") != 0 ? flags.values.at("--format") : "sync";
    if (format != "sync" && format != "complete") {
        throw ToolError(ExitCode::Usage, "FORMAT",
                        "--format must be sync or complete");
    }
    LocalContext context = open_context(parsed, in, out, false);
    const json payload = normalize_sync_payload(context.payload);
    const json exported = format == "complete"
                              ? export_complete_envelope(payload, iso_now())
                              : payload;
    std::string text = exported.dump(2);
    text.push_back('\n');
    if (out_path == "-") {
        out << text;
        return 0;
    }
    const std::string password = read_vault_password(parsed.options, in, out, false);
    seal_encrypted_file(out_path, "HRTE", text, password);
    out << "exported: " << out_path << "\n";
    return 0;
}

int handle_import(const ParsedArgs &parsed, std::istream &in, std::ostream &out) {
    const CommandFlags flags = parse_command_flags(
        parsed.command_args, {"--in", "--format"}, {"--replace"});
    const std::string in_path = require_value(flags, "--in");
    const std::string format =
        flags.values.count("--format") != 0 ? flags.values.at("--format") : "auto";
    if (format != "auto" && format != "sync" && format != "complete") {
        throw ToolError(ExitCode::Usage, "FORMAT",
                        "--format must be auto, sync, or complete");
    }

    std::string text;
    {
        std::error_code type_error;
        if (!std::filesystem::is_regular_file(in_path, type_error)) {
            throw ToolError(ExitCode::Data, "FILE_TYPE",
                            "import file is not a regular file: " + in_path);
        }
        std::ifstream probe(in_path, std::ios::binary);
        if (!probe) {
            throw ToolError(ExitCode::Usage, "IMPORT", "cannot open " + in_path);
        }
        char magic[4] = {};
        probe.read(magic, 4);
        if (probe.gcount() == 4 && std::memcmp(magic, "HRTE", 4) == 0) {
            const std::string password =
                read_vault_password(parsed.options, in, out, false);
            text = open_encrypted_file(in_path, "HRTE", password);
        } else {
            text = read_text_file_capped(in_path, 16 * 1024 * 1024, "import file");
        }
    }
    json imported_json = json::parse(text.begin(), text.end(), nullptr, false);
    if (imported_json.is_discarded() ||
        (!imported_json.is_object() && !imported_json.is_array())) {
        throw ToolError(ExitCode::Data, "IMPORT", "import file is not valid JSON");
    }
    const bool is_envelope =
        imported_json.is_object() && imported_json.contains("format") &&
        imported_json.at("format").is_string() &&
        imported_json.at("format").get<std::string>() == "oyama-hrt-complete-backup";
    if (format == "complete" && !is_envelope) {
        throw ToolError(ExitCode::Data, "IMPORT", "input is not a complete backup");
    }
    if (format == "sync" && is_envelope) {
        throw ToolError(ExitCode::Data, "IMPORT", "input is a complete backup, not sync");
    }

    json imported;
    std::vector<std::string> not_carried;
    if (is_envelope) {
        EnvelopeImport envelope = import_complete_envelope(imported_json);
        imported = envelope.sync_payload;
        not_carried = envelope.not_carried;
    } else {
        imported = normalize_sync_payload(imported_json);
    }
    ImportSanitizeReport sanitize_report;
    imported = sanitize_import_payload(imported, &sanitize_report);

    LocalContext context = open_context(parsed, in, out, true);
    if (flags.flags.count("--replace") != 0) {
        if (!parsed.options.yes) {
            out << "Replace the snapshot? Type 'yes' to continue: ";
            out.flush();
            std::string answer;
            if (!std::getline(in, answer)) {
                throw ToolError(ExitCode::Usage, "REPLACE_ABORTED",
                                "snapshot replacement cancelled");
            }
            if (!answer.empty() && answer.back() == '\r') {
                answer.pop_back();
            }
            if (answer != "yes") {
                throw ToolError(ExitCode::Usage, "REPLACE_ABORTED",
                                "snapshot replacement cancelled");
            }
        }
        context.payload = imported;
        save_context(context);
        out << "replaced snapshot\n";
    } else {
        MergeOptions options;
        options.explicit_import = true;
        MergeResult merge = merge_sync_payloads(context.payload, imported, options);
        if (merge.aborted) {
            std::string message = "merge aborted:";
            for (const std::string &warning : merge.warnings) {
                message += " " + warning;
            }
            throw ToolError(ExitCode::Data, "MERGE_ABORTED", message);
        }
        context.payload = merge.payload;
        save_context(context);
        out << "merged: added " << merge.stats.added << ", updated " << merge.stats.updated
            << ", removed " << merge.stats.removed << "\n";
    }
    if (sanitize_report.dropped > 0) {
        out << "sanitized: dropped " << sanitize_report.dropped
            << " invalid record(s)\n";
    }
    if (sanitize_report.coerced > 0) {
        out << "sanitized: coerced " << sanitize_report.coerced << " record(s)\n";
    }
    for (const std::string &category : not_carried) {
        out << "not carried by the cloud sync payload: " << category << "\n";
    }
    return 0;
}

} // namespace

bool is_local_command(const std::string &command) {
    return command == "look" || command == "add" || command == "dose" ||
           command == "lab" || command == "weight" || command == "export" ||
           command == "import";
}

int handle_local_command(const ParsedArgs &parsed, std::istream &in, std::ostream &out,
                         std::ostream &err) {
    if (parsed.command == "look") {
        return handle_look(parsed, in, out);
    }
    if (parsed.command == "add") {
        return handle_add(parsed, in, out, err);
    }
    if (parsed.command == "dose") {
        return handle_dose(parsed, in, out);
    }
    if (parsed.command == "lab") {
        return handle_lab(parsed, in, out);
    }
    if (parsed.command == "weight") {
        return handle_weight(parsed, in, out);
    }
    if (parsed.command == "export") {
        return handle_export(parsed, in, out);
    }
    if (parsed.command == "import") {
        return handle_import(parsed, in, out);
    }
    throw ToolError(ExitCode::Usage, "USAGE", "unknown local command");
}
