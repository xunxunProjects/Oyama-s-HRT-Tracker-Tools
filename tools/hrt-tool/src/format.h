#pragma once

#include <string>

std::string format_time(double hours, bool utc);        // existing look format
std::string format_time_status(double hours, bool utc); // "UTC +0 1970-01-01 00:00"
std::string format_duration(double hours);
std::string format_elapsed(double hours); // "1d , 11h , 10min"
std::string format_dose(double mg);       // "2mg"
std::string format_concentration(double value, int ester);
std::string format_lab_value(double value, const std::string &unit);
std::string format_lab_value_status(double value, const std::string &unit);
std::string format_assay(const std::string &assay);
