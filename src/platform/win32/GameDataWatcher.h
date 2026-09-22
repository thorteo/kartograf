#pragma once

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

#include "core/data/QuestProgress.h"
#include "core/game/LogParser.h"
#include "core/game/QuestEvents.h"
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

    /// Called on the watcher thread after quest progress changed, so it can be
    /// written to disk. The game only keeps about five days of logs; what we
    /// have read has to be kept by us or it is lost.
    using ProgressChanged = std::function<void()>;

    bool start(game::RaidState& state, Logger logger = {},
               data::QuestProgressStore* progress = nullptr,
               ProgressChanged onProgress = {});
    void stop();

    const GamePaths& paths() const { return paths_; }

private:
    void run(game::RaidState* state);
    void openNewestLogFolder();
    /// Opens whichever of the current session's logs exist and are not open.
    ///
    /// The game creates them one at a time, not with the folder. Measured on
    /// three sessions on 2026-09-22: output_000.log 17 ms after the folder,
    /// application_000.log 1.4-1.7 s, push-notifications_000.log 48 s to
    /// 2.5 min. Looking once, the moment the folder appeared, found none of
    /// them, and a session started after the overlay went unread.
    void openMissingLogs();
    /// Reads the quest events out of every log session still on disk. Runs
    /// once at startup; later events arrive through the live tail.
    void importQuestHistory();
    void pumpNotifications();
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
    /// Set when a session's logs have just been opened from the start: the next
    /// pump replays what happened before we were watching, quietly.
    bool catchingUp_ = false;

    data::QuestProgressStore* progress_ = nullptr;
    ProgressChanged onProgress_;
    LogTail notificationsLog_;
    game::QuestEventParser notificationsParser_;
};

}  // namespace tarkov::win32
