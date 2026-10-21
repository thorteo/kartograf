#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tarkov::win32 {

/// %LOCALAPPDATA%\kartograf\<leaf>: settings, progress and the caches.
/// Falls back to the temporary directory if the known folder is unavailable,
/// and to the folder of the program's old name while that has not been moved.
std::filesystem::path appDataPath(std::wstring_view leaf);

/// Moves %LOCALAPPDATA%\tarkov-help, the folder of the program's old name, to
/// %LOCALAPPDATA%\kartograf, when only the old one exists.
///
/// Called once at startup, before anything is read from either, and only when
/// no copy under the old name is running. A move that fails leaves everything
/// where it was, and appDataPath keeps using the old folder: it holds quest
/// corrections and the originals of the icons the price labels replaced, which
/// exist nowhere else.
void migrateLegacyAppData();

/// The whole file, or empty when it cannot be read.
std::string readTextFile(const std::filesystem::path& path);
std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path);

/// Writes to `<path>.part`, then renames over `path`, creating the directory
/// first. A crash or a full disk mid-write leaves the old file, never a
/// truncated one that would be read back as the cache forever afterwards.
bool writeFileAtomic(const std::filesystem::path& path, std::string_view data);

}  // namespace tarkov::win32
