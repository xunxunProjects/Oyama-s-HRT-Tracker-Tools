#include "format.h"

#include "hrtcore/pk.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace {

bool is_t_ester(int ester) {
    return ester == HRTCORE_ESTER_T || ester == HRTCORE_ESTER_TC ||
           ester == HRTCORE_ESTER_TE || ester == HRTCORE_ESTER_TU;
}

std::time_t epoch_from_tm(std::tm &tm) {
#ifdef _WIN32
    return _mkgmtime(&tm);
#else
    return ::timegm(&tm);
#endif
}

} // namespace

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

std::string format_time_status(double hours, bool utc) {
    const std::time_t seconds = static_cast<std::time_t>(hours * 3600.0);
    std::tm display{};
#ifdef _WIN32
    if (utc) {
        gmtime_s(&display, &seconds);
    } else {
        localtime_s(&display, &seconds);
    }
#else
    if (utc) {
        gmtime_r(&seconds, &display);
    } else {
        localtime_r(&seconds, &display);
    }
#endif

    int offset_minutes = 0;
    if (!utc) {
        std::tm local_tm{};
        std::tm gm_tm{};
#ifdef _WIN32
        localtime_s(&local_tm, &seconds);
        gmtime_s(&gm_tm, &seconds);
#else
        localtime_r(&seconds, &local_tm);
        gmtime_r(&seconds, &gm_tm);
#endif
        const std::time_t local_as_utc = epoch_from_tm(local_tm);
        const std::time_t gm_as_utc = epoch_from_tm(gm_tm);
        offset_minutes = static_cast<int>((local_as_utc - gm_as_utc) / 60);
    }

    const char sign = offset_minutes < 0 ? '-' : '+';
    const int abs_minutes = offset_minutes < 0 ? -offset_minutes : offset_minutes;
    const int hours_part = abs_minutes / 60;
    const int minutes_part = abs_minutes % 60;

    char buffer[64] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &display);
    std::ostringstream out;
    out << "UTC " << sign << hours_part;
    if (minutes_part != 0) {
        out << ':' << std::setw(2) << std::setfill('0') << minutes_part;
    }
    out << ' ' << buffer;
    return out.str();
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

std::string format_elapsed(double hours) {
    if (!std::isfinite(hours) || hours < 0.0) {
        hours = 0.0;
    }
    const int total_minutes = static_cast<int>(std::floor(hours * 60.0));
    const int days = total_minutes / (24 * 60);
    const int hour_part = (total_minutes / 60) % 24;
    const int minutes = total_minutes % 60;
    std::ostringstream out;
    if (days > 0) {
        out << days << "d , ";
    }
    out << hour_part << "h , " << minutes << "min";
    return out.str();
}

std::string format_dose(double mg) {
    std::ostringstream out;
    out << std::setprecision(6) << std::defaultfloat << mg << "mg";
    return out.str();
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

std::string format_lab_value_status(double value, const std::string &unit) {
    std::ostringstream out;
    out << std::fixed;
    if (unit == "pg/ml") {
        out << std::setprecision(1) << value << " pg/mL \\ " << std::setprecision(1)
            << value * 3.671 << " pmol/L";
    } else if (unit == "pmol/l") {
        out << std::setprecision(1) << value << " pmol/L \\ " << std::setprecision(1)
            << value / 3.671 << " pg/mL";
    } else if (unit == "ng/dl") {
        out << std::setprecision(1) << value << " ng/dL \\ " << std::setprecision(2)
            << value / 28.842 << " nmol/L";
    } else if (unit == "nmol/l") {
        out << std::setprecision(2) << value << " nmol/L \\ " << std::setprecision(1)
            << value * 28.842 << " ng/dL";
    } else {
        out << std::setprecision(1) << value << " " << unit;
    }
    return out.str();
}

std::string format_assay(const std::string &assay) {
    if (assay == "lcms") {
        return "Mass spectrometry";
    }
    if (assay == "immuno") {
        return "Immunoassay";
    }
    return assay;
}
