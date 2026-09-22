#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tarkov::win32 {

/// %LOCALAPPDATA%\tarkov-help\<leaf>: settings, progress and the caches.
/// Falls back to the temporary directory if the known folder is unavailable.
std::filesystem::path appDataPath(std::wstring_view leaf);

/// The whole file, or empty when it cannot be read.
std::string readTextFile(const std::filesystem::path& path);
std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path);

/// Writes to `<path>.part`, then renames over `path`, creating the directory
/// first. A crash or a full disk mid-write leaves the old file, never a
/// truncated one that would be read back as the cache forever afterwards.
bool writeFileAtomic(const std::filesystem::path& path, std::string_view data);

}  // namespace tarkov::win32
