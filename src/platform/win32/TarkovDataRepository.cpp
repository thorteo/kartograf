#include "ui/Strings.h"
#include "platform/win32/TarkovDataRepository.h"

#include <ctime>

#include "core/data/MapIds.h"
#include "platform/win32/Files.h"

namespace tarkov::win32 {
namespace {

namespace fs = std::filesystem;

constexpr const char* kHost = "https://json.tarkov.dev";

/// How old the cache may be before a refresh is attempted. Quest data changes
/// on wipes and patches, not hourly.
constexpr long long kMaxAgeSeconds = 12 * 60 * 60;

/// How long after a download that lacked an optional document -- a locale,
/// the maps -- the whole set may be fetched again to fill it in. Retrying
/// every half hour re-downloaded everything for as long as one endpoint was
/// down.
constexpr long long kOptionalRetrySeconds = 6 * 60 * 60;

long long nowUnix() {
    return static_cast<long long>(std::time(nullptr));
}

}  // namespace

TarkovDataRepository::~TarkovDataRepository() { stop(); }

fs::path TarkovDataRepository::cacheDir(const std::string& mode) const {
    return cacheRoot_ / mode;
}

void TarkovDataRepository::say(const std::string& message) const {
    if (logger_) {
        logger_(message);
    }
}

bool TarkovDataRepository::start(std::string gameMode, std::string language,
                           Logger logger) {
    logger_ = std::move(logger);
    language_ = std::move(language);
    cacheRoot_ = appDataPath(L"data");
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        gameMode_ = gameMode;
        wantedMode_ = std::move(gameMode);
    }
    stopping_.store(false, std::memory_order_relaxed);
    worker_ = std::thread(&TarkovDataRepository::run, this);
    return true;
}

void TarkovDataRepository::stop() {
    if (!worker_.joinable()) {
        return;
    }
    {
        // Under the lock, or the worker between its check and its wait misses
        // both the flag and the notification and sleeps out its half hour.
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_.store(true, std::memory_order_relaxed);
    }
    wake_.notify_all();
    cancel_.cancel();  // Quitting mid-download must not wait for it.
    worker_.join();
}

void TarkovDataRepository::setGameMode(std::string gameMode) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (wantedMode_ == gameMode) {
            return;
        }
        wantedMode_ = std::move(gameMode);
    }
    wake_.notify_one();
}

void TarkovDataRepository::setLanguage(std::string language) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (language_ == language) {
            return;
        }
        // Under the lock: the worker reads language_ while downloading, and
        // an unguarded write from the UI thread would be a data race.
        language_ = std::move(language);
        reloadRequested_ = true;
    }
    wake_.notify_one();
}

void TarkovDataRepository::setInRaid(bool inRaid) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (inRaid_ == inRaid) {
            return;
        }
        inRaid_ = inRaid;
    }
    wake_.notify_one();  // A refresh held back for the raid can go now.
}

std::shared_ptr<const data::TaskSet> TarkovDataRepository::tasks() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return published_;
}

std::shared_ptr<const TarkovDataRepository::MapFeatureSet>
TarkovDataRepository::mapFeatures() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return publishedMaps_;
}

TarkovDataRepository::Status TarkovDataRepository::status() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    Status status;
    status.haveData = !published_->tasks.empty();
    status.fetching = fetching_;
    status.ageSeconds = writtenAtUnix_ == 0 ? -1 : nowUnix() - writtenAtUnix_;
    return status;
}

void TarkovDataRepository::publish(data::TaskSet set, MapFeatureSet features,
                                   long long writtenAtUnix) {
    const std::lock_guard<std::mutex> lock(mutex_);
    published_ = std::make_shared<const data::TaskSet>(std::move(set));
    publishedMaps_ = std::make_shared<const MapFeatureSet>(std::move(features));
    writtenAtUnix_ = writtenAtUnix;
}

bool TarkovDataRepository::loadFromCache(const std::string& mode) {
    std::string language;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        language = language_;
    }
    const fs::path dir = cacheDir(mode);
    const std::string body = readTextFile(dir / "tasks.json");
    if (body.empty()) {
        return false;
    }

    const data::TaskLocale locale =
        data::TaskLocale::parse(readTextFile(dir / ("tasks_" + language + ".json")));
    const data::TaskLocale itemLocale =
        data::TaskLocale::parse(readTextFile(dir / ("items_" + language + ".json")));
    data::TaskSet set = data::parseTasks(body, locale, itemLocale);
    if (set.tasks.empty()) {
        // A cache that parses to nothing is worse than none: it would sit there
        // looking valid. Drop it so the next start re-downloads.
        std::error_code ec;
        fs::remove(dir / "tasks.json", ec);
        return false;
    }

    // Map features are optional: an old cache from before they were fetched
    // still gives quests, and the next refresh fills in the rest.
    const data::TaskLocale mapLocale =
        data::TaskLocale::parse(readTextFile(dir / ("maps_" + language + ".json")));
    MapFeatureSet features =
        data::parseMapFeatures(readTextFile(dir / "maps.json"), mapLocale);

    long long writtenAt = 0;
    const std::string stamp = readTextFile(dir / "tasks.updated");
    if (!stamp.empty()) {
        writtenAt = std::strtoll(stamp.c_str(), nullptr, 10);
    }

    const std::size_t count = set.tasks.size();
    {
        // Explicit, not encoded in the timestamp: a sentinel age is how a
        // cache once never refreshed. Checked by run().
        const std::lock_guard<std::mutex> lock(mutex_);
        // Item names came later than quest names; a cache from before them
        // counts as untranslated, so the next pass fetches what is missing.
        localeMissing_ = locale.size() == 0 || itemLocale.size() == 0;
    }
    publish(std::move(set), std::move(features), writtenAt);
    say(ui::trf(ui::Str::StatusQuestsFromCache, static_cast<int>(count)));
    return true;
}

bool TarkovDataRepository::downloadAndCache(const std::string& mode) {
    std::string language;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        language = language_;
    }
    HttpClient client;
    if (!client.open(&cancel_)) {
        return false;
    }

    const std::string base = std::string(kHost) + "/" + mode;
    // The task document is a couple of megabytes; allow room but not unbounded.
    const auto tasksResponse = client.get(base + "/tasks", 32u * 1024 * 1024);
    if (!tasksResponse.ok) {
        if (!cancel_.cancelled()) {
            say(ui::trf(ui::Str::StatusQuestsDownloadFailed,
                        static_cast<int>(tasksResponse.status)));
        }
        return false;
    }

    const std::string body(tasksResponse.body.begin(), tasksResponse.body.end());

    // The locale is a separate document; without it every quest would be
    // titled with a hex id. Its absence is survivable, so it is not fatal.
    std::string localeBody;
    const auto localeResponse =
        client.get(base + "/tasks_" + language, 16u * 1024 * 1024);
    if (localeResponse.ok) {
        localeBody.assign(localeResponse.body.begin(), localeResponse.body.end());
    } else {
        say(ui::trf(ui::Str::StatusNoTranslations, language.c_str()));
    }

    // Item names, for what an objective needs brought or handed over. 2.4 MB
    // and optional: without it the tooltips just leave the items out.
    std::string itemLocaleBody;
    const auto itemLocaleResponse =
        client.get(base + "/items_" + language, 16u * 1024 * 1024);
    if (itemLocaleResponse.ok) {
        itemLocaleBody.assign(itemLocaleResponse.body.begin(),
                              itemLocaleResponse.body.end());
    }

    const data::TaskLocale locale = data::TaskLocale::parse(localeBody);
    data::TaskSet set =
        data::parseTasks(body, locale, data::TaskLocale::parse(itemLocaleBody));
    if (set.tasks.empty()) {
        say(ui::tr(ui::Str::StatusQuestsDidNotParse));
        return false;
    }

    // The maps document is large (8.5 MB, mostly loot positions we do not
    // draw) but it is fetched rarely and is the only source of extracts and
    // spawns. A failure here costs those layers, not the quests.
    std::string mapsBody;
    std::string mapsLocaleBody;
    const auto mapsResponse = client.get(base + "/maps", 64u * 1024 * 1024);
    if (mapsResponse.ok) {
        mapsBody.assign(mapsResponse.body.begin(), mapsResponse.body.end());
        const auto mapsLocale =
            client.get(base + "/maps_" + language, 16u * 1024 * 1024);
        if (mapsLocale.ok) {
            mapsLocaleBody.assign(mapsLocale.body.begin(), mapsLocale.body.end());
        }
    } else {
        say(ui::tr(ui::Str::StatusMapsUnavailable));
    }
    MapFeatureSet features = data::parseMapFeatures(
        mapsBody, data::TaskLocale::parse(mapsLocaleBody));

    const fs::path dir = cacheDir(mode);
    const long long writtenAt = nowUnix();
    if (!mapsBody.empty()) {
        writeFileAtomic(dir / "maps.json", mapsBody);
    }
    if (!mapsLocaleBody.empty()) {
        writeFileAtomic(dir / ("maps_" + language + ".json"), mapsLocaleBody);
    }
    writeFileAtomic(dir / "tasks.json", body);
    if (!localeBody.empty()) {
        writeFileAtomic(dir / ("tasks_" + language + ".json"), localeBody);
    }
    if (!itemLocaleBody.empty()) {
        writeFileAtomic(dir / ("items_" + language + ".json"), itemLocaleBody);
    }
    writeFileAtomic(dir / "tasks.updated", std::to_string(writtenAt));
    {
        // What this download actually brought. Left as the cache load set it,
        // a missing locale would trigger a full re-download every half hour.
        const std::lock_guard<std::mutex> lock(mutex_);
        localeMissing_ = localeBody.empty() || itemLocaleBody.empty();
        optionalRetryAtUnix_ = writtenAt + kOptionalRetrySeconds;
    }

    const std::size_t count = set.tasks.size();
    const std::size_t mapCount = features.size();
    publish(std::move(set), std::move(features), writtenAt);
    say(ui::trf(ui::Str::StatusDataDownloaded, static_cast<int>(count),
                static_cast<int>(mapCount)));
    return true;
}

void TarkovDataRepository::run() {
    std::string loadedMode;

    while (!stopping_.load(std::memory_order_relaxed)) {
        std::string mode;
        bool wantRefresh = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            mode = wantedMode_;
            if (reloadRequested_) {
                reloadRequested_ = false;
                loadedMode.clear();  // re-read the cache through the new locale
            }
            gameMode_ = mode;
        }

        const bool modeChanged = mode != loadedMode;
        if (modeChanged) {
            loadedMode = mode;
            if (!loadFromCache(mode)) {
                wantRefresh = true;
            }
        }

        // A refresh of data that is merely old, or missing an optional part,
        // waits for the raid to end: several megabytes competing with the game
        // for bandwidth mid-fight is the wrong trade. With no data at all, or
        // none for this mode, it cannot wait.
        bool deferred = false;
        if (!wantRefresh) {
            const auto current = status();
            bool mapsMissing = false;
            bool localeMissing = false;
            bool optionalDue = false;
            bool inRaid = false;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                mapsMissing = publishedMaps_->empty();
                localeMissing = localeMissing_;
                optionalDue = nowUnix() >= optionalRetryAtUnix_;
                inRaid = inRaid_;
            }
            // Refresh when there is nothing, when its age is unknown, when it
            // is old, or when a cache from before map data existed lacks it.
            // An unknown age must count as stale: treating it as fresh is how
            // an older cache once kept extracts from ever being downloaded.
            const bool stale = current.ageSeconds < 0 || current.ageSeconds > kMaxAgeSeconds;
            const bool incomplete = (mapsMissing || localeMissing) && optionalDue;
            if (!current.haveData) {
                wantRefresh = true;
            } else if (stale || incomplete) {
                wantRefresh = !inRaid;
                deferred = inRaid;
            }
        }

        if (wantRefresh && !stopping_.load(std::memory_order_relaxed)) {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                fetching_ = true;
            }
            downloadAndCache(mode);
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                fetching_ = false;
            }
        }

        std::unique_lock<std::mutex> lock(mutex_);
        // Wake on a mode change, a language change, the end of a raid that
        // held a refresh back, or shutdown; otherwise sleep long. There is
        // nothing to poll for.
        wake_.wait_for(lock, std::chrono::minutes(30), [this, deferred] {
            return stopping_.load(std::memory_order_relaxed) || reloadRequested_ ||
                   wantedMode_ != gameMode_ || (deferred && !inRaid_);
        });
    }
}

}  // namespace tarkov::win32
