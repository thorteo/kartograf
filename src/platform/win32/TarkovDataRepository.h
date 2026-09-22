#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <map>
#include <thread>

#include "core/data/MapFeatures.h"
#include "core/data/TaskModel.h"
#include "platform/win32/HttpClient.h"

namespace tarkov::win32 {

/// Keeps tarkov.dev's quest and map data on disk and in memory.
///
/// Downloads the task and map documents with their locales from the JSON API,
/// writes them beside a timestamp, and serves them from the cache afterwards. Quest data is not compiled into the
/// binary because it changes every wipe, and a stale marker is worse than none.
///
/// Threading follows ADR 0004: one worker writes, readers take an immutable
/// snapshot.
class TarkovDataRepository {
public:
    using Logger = std::function<void(const std::string&)>;

    struct Status {
        bool haveData = false;
        bool fetching = false;
        /// Seconds since the cached copy was written, or -1 when there is none.
        long long ageSeconds = -1;
    };

    ~TarkovDataRepository();

    TarkovDataRepository() = default;
    TarkovDataRepository(const TarkovDataRepository&) = delete;
    TarkovDataRepository& operator=(const TarkovDataRepository&) = delete;

    /// Loads whatever is cached, then refreshes in the background if it is
    /// missing or old. `language` is a tarkov.dev locale code such as "ru".
    bool start(std::string gameMode, std::string language, Logger logger = {});
    void stop();

    /// Switches dataset, e.g. when a PvE raid starts. Loads that mode's cache
    /// and refreshes it if needed. Cheap and safe to call repeatedly.
    void setGameMode(std::string gameMode);

    /// Switches the locale quest and extract names are shown in. Reloads from
    /// the cache and downloads the other locale if it has not been fetched.
    void setLanguage(std::string language);

    /// Whether a raid is on. Refreshes of data that is only old wait for it to
    /// end: a multi-megabyte download mid-raid competes with the game for
    /// bandwidth. Data that is missing outright is fetched regardless.
    void setInRaid(bool inRaid);

    /// A coherent view. Never null; empty until the first load succeeds.
    std::shared_ptr<const data::TaskSet> tasks() const;

    using MapFeatureSet = data::MapFeatureSet;
    /// Extracts, spawns and transits, keyed by BSG map id. Never null.
    std::shared_ptr<const MapFeatureSet> mapFeatures() const;

    Status status() const;

private:
    void run();
    bool loadFromCache(const std::string& mode);
    bool downloadAndCache(const std::string& mode);
    void publish(data::TaskSet set, MapFeatureSet features, long long writtenAtUnix);
    void say(const std::string& message) const;

    std::filesystem::path cacheDir(const std::string& mode) const;

    Logger logger_;
    std::string language_ = "en";

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> stopping_{false};
    HttpCancel cancel_;
    std::thread worker_;

    std::string gameMode_ = "regular";
    std::string wantedMode_ = "regular";
    bool inRaid_ = false;
    /// Unix time after which a download that came back without an optional
    /// document may be repeated to fill it in. Zero: not yet tried.
    long long optionalRetryAtUnix_ = 0;
    /// Set when the language changed: the cache must be re-read so names come
    /// through the new locale.
    bool reloadRequested_ = false;
    /// The cached copy has no translations for the current language.
    bool localeMissing_ = false;
    bool fetching_ = false;
    long long writtenAtUnix_ = 0;

    std::shared_ptr<const data::TaskSet> published_{
        std::make_shared<const data::TaskSet>()};
    std::shared_ptr<const MapFeatureSet> publishedMaps_{
        std::make_shared<const MapFeatureSet>()};

    std::filesystem::path cacheRoot_;
};

}  // namespace tarkov::win32
