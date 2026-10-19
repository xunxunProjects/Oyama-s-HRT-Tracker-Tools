#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

/*
 * Bounded readers for local data files.
 *
 * Each reader refuses non-regular files and reads at most max_bytes + 1 bytes,
 * so a file that grows after a stat, a FIFO, or a device cannot exhaust memory.
 * Exceeding the cap throws ToolLimitError; an unreadable file throws a typed
 * ToolError.
 */

std::string read_text_file_capped(const std::filesystem::path &path, size_t max_bytes,
                                  const std::string &what);

std::vector<uint8_t> read_binary_file_capped(const std::filesystem::path &path,
                                             size_t max_bytes, const std::string &what);
