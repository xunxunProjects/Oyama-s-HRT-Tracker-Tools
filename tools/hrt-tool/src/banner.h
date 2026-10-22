#pragma once

#include "term.h"

#include <string>

// Wordmark beside the title when the terminal is wide enough (19 columns plus the longer of
// the title and product lines), otherwise a two-line form. The text ends with a newline.
std::string render_banner(const TermCaps &caps, const std::string &version,
                          const std::string &hint);

// A custom vault welcome wins, then --json or a non-terminal gets the plain hint, else the banner.
std::string console_welcome(const TermCaps &caps, bool json, const std::string &custom,
                            const std::string &hint, const std::string &version);
