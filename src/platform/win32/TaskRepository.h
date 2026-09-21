#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/data/TaskModel.h"

namespace tarkov::win32 {

/// Keeps the quest data on disk and in memory.
///
/// Downloads once from tarkov.dev's JSON API, writes it beside a timestamp, and
/// serves it from the cache afterwards. Quest data is not compiled into the
/// binary because it changes every wipe, and a stale marker is worse than none.
///
/// Threading follows ADR 0004: one worker writes, readers take an immutable
/// snapshot.
class TaskRepository {
public:
    using Logger = std::function<void(const std::string&)>;

    struct Status {
        bool haveData = false;
        bool fetching = false;
        /// Seconds since the cached copy was written, or -1 when there is none.
        long long ageSeconds = -1;
        std::string detail;
    };

    ~TaskRepository();

    TaskRepository() = default;
    TaskRepository(const TaskRepository&) = delete;
    TaskRepository& operator=(const TaskRepository&) = delete;

    /// Loads whatever is cached, then refreshes in the background if it is
    /// missing or old. `language` is a tarkov.dev locale code such as "ru".
    bool start(std::string gameMode, std::string language, Logger logger = {});
    void stop();

    /// Switches dataset, e.g. when a PvE raid starts. Loads that mode's cache
    /// and refreshes it if needed. Cheap and safe to call repeatedly.
    void setGameMode(std::string gameMode);

    /// Asks for a refresh. Ignored while one is already running.
    ///
    /// The caller decides when this is appropriate: never mid-raid, where a
    /// multi-megabyte download would be competing with the game for bandwidth.
    void refresh();

    /// A coherent view. Never null; empty until the first load succeeds.
    std::shared_ptr<const data::TaskSet> tasks() const;

    Status status() const;

    static std::filesystem::path defaultCacheRoot();

private:
    void run();
    bool loadFromCache(const std::string& mode);
    bool downloadAndCache(const std::string& mode);
    void publish(data::TaskSet set, long long writtenAtUnix);
    void say(const std::string& message) const;

    std::filesystem::path cacheDir(const std::string& mode) const;

    Logger logger_;
    std::string language_ = "en";

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> stopping_{false};
    std::thread worker_;

    std::string gameMode_ = "regular";
    std::string wantedMode_ = "regular";
    bool refreshRequested_ = false;
    bool fetching_ = false;
    std::string detail_;
    long long writtenAtUnix_ = 0;

    std::shared_ptr<const data::TaskSet> published_{
        std::make_shared<const data::TaskSet>()};

    std::filesystem::path cacheRoot_;
};

}  // namespace tarkov::win32
