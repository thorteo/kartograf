#pragma once

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

#include "core/game/LogParser.h"
#include "core/game/RaidState.h"
#include "platform/win32/GameFiles.h"

namespace tarkov::win32 {

/// Watches the game's log and screenshot directories and feeds RaidState.
///
/// Runs one background thread. It is the only writer to RaidState, exactly as
/// docs/adr/0004-threading-model.md requires: the render thread never mutates
/// state, it only takes snapshots.
///
/// Everything here reads files the game writes. Nothing opens the game process.
class GameDataWatcher {
public:
    /// Optional sink for status lines, called from the watcher thread.
    using Logger = std::function<void(const std::string&)>;

    ~GameDataWatcher();

    GameDataWatcher() = default;
    GameDataWatcher(const GameDataWatcher&) = delete;
    GameDataWatcher& operator=(const GameDataWatcher&) = delete;

    bool start(game::RaidState& state, Logger logger = {});
    void stop();

    const GamePaths& paths() const { return paths_; }

private:
    void run(game::RaidState* state);
    void openNewestLogFolder();
    void pumpLogs(game::RaidState& state);
    void handleScreenshotNames(game::RaidState& state,
                               const std::vector<std::wstring>& names);
    void say(const std::string& message) const;

    GamePaths paths_;
    Logger logger_;

    std::thread thread_;
    std::atomic<bool> stopping_{false};
    HANDLE stopEvent_ = nullptr;

    std::filesystem::path currentLogFolder_;
    LogTail applicationLog_;
    LogTail outputLog_;
    game::LogParser applicationParser_;
    game::LogParser outputParser_;
};

}  // namespace tarkov::win32
