#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/game/LineSplitter.h"

namespace tarkov::win32 {

/// Where Escape from Tarkov keeps the two things we read.
struct GamePaths {
    std::filesystem::path logsRoot;     ///< <install>\Logs, holding log_* folders
    std::filesystem::path screenshots;  ///< Documents\Escape from Tarkov\Screenshots
};

/// Locates the game's log and screenshot directories.
///
/// Deliberately does not inspect the running game process -- finding the
/// install path by opening the process would violate ADR 0002 for no good
/// reason. Instead: the registry's uninstall entry, then the usual install
/// locations on each fixed drive.
///
/// Either path may be missing; the caller should work with whichever it got.
std::optional<GamePaths> discoverGamePaths();

/// The newest `log_*` folder under a logs root, by name.
///
/// Folder names begin with a sortable timestamp, so lexicographic order is
/// chronological order. A new folder appears on every game launch.
std::optional<std::filesystem::path> newestLogFolder(
    const std::filesystem::path& logsRoot);

/// Follows a growing log file, yielding complete lines as they are appended.
///
/// The game keeps the file open and appends to it. A partial trailing line is
/// held back until its newline arrives.
class LogTail {
public:
    ~LogTail();

    LogTail() = default;
    LogTail(const LogTail&) = delete;
    LogTail& operator=(const LogTail&) = delete;

    /// Opens the file, positioned at its end unless `fromStart`.
    bool open(const std::filesystem::path& path, bool fromStart = false);
    void close();
    bool isOpen() const { return handle_ != INVALID_HANDLE_VALUE; }

    const std::filesystem::path& path() const { return path_; }

    /// Reads whatever has been appended since the last call.
    std::vector<std::string> readNewLines();

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::filesystem::path path_;
    game::LineSplitter lines_;
};

}  // namespace tarkov::win32
