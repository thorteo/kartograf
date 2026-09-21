#include "platform/win32/TaskRepository.h"

#include <windows.h>

#include <shlobj.h>

#include <ctime>
#include <fstream>
#include <sstream>

#include "core/data/MapIds.h"
#include "platform/win32/HttpClient.h"

namespace tarkov::win32 {
namespace {

namespace fs = std::filesystem;

constexpr const char* kHost = "https://json.tarkov.dev";

/// How old the cache may be before a refresh is attempted. Quest data changes
/// on wipes and patches, not hourly.
constexpr long long kMaxAgeSeconds = 12 * 60 * 60;

long long nowUnix() {
    return static_cast<long long>(std::time(nullptr));
}

std::string readTextFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool writeTextFile(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    // Write then rename, so an interrupted download cannot leave a truncated
    // document to be parsed as the cache forever afterwards.
    const fs::path temporary = path.string() + ".part";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            return false;
        }
    }
    fs::rename(temporary, path, ec);
    if (ec) {
        fs::remove(temporary, ec);
        return false;
    }
    return true;
}

}  // namespace

TaskRepository::~TaskRepository() { stop(); }

fs::path TaskRepository::defaultCacheRoot() {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        fs::path path(raw);
        CoTaskMemFree(raw);
        return path / "tarkov-help" / "data";
    }
    return fs::temp_directory_path() / "tarkov-help" / "data";
}

fs::path TaskRepository::cacheDir(const std::string& mode) const {
    return cacheRoot_ / mode;
}

void TaskRepository::say(const std::string& message) const {
    if (logger_) {
        logger_(message);
    }
}

bool TaskRepository::start(std::string gameMode, std::string language,
                           Logger logger) {
    logger_ = std::move(logger);
    language_ = std::move(language);
    cacheRoot_ = defaultCacheRoot();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        gameMode_ = gameMode;
        wantedMode_ = std::move(gameMode);
    }
    stopping_.store(false, std::memory_order_relaxed);
    worker_ = std::thread(&TaskRepository::run, this);
    return true;
}

void TaskRepository::stop() {
    if (!worker_.joinable()) {
        return;
    }
    stopping_.store(true, std::memory_order_relaxed);
    wake_.notify_all();
    worker_.join();
}

void TaskRepository::setGameMode(std::string gameMode) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (wantedMode_ == gameMode) {
            return;
        }
        wantedMode_ = std::move(gameMode);
    }
    wake_.notify_one();
}

void TaskRepository::refresh() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (fetching_) {
            return;
        }
        refreshRequested_ = true;
    }
    wake_.notify_one();
}

std::shared_ptr<const data::TaskSet> TaskRepository::tasks() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return published_;
}

TaskRepository::Status TaskRepository::status() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    Status status;
    status.haveData = !published_->tasks.empty();
    status.fetching = fetching_;
    status.ageSeconds = writtenAtUnix_ == 0 ? -1 : nowUnix() - writtenAtUnix_;
    status.detail = detail_;
    return status;
}

void TaskRepository::publish(data::TaskSet set, long long writtenAtUnix) {
    const std::lock_guard<std::mutex> lock(mutex_);
    published_ = std::make_shared<const data::TaskSet>(std::move(set));
    writtenAtUnix_ = writtenAtUnix;
}

bool TaskRepository::loadFromCache(const std::string& mode) {
    const fs::path dir = cacheDir(mode);
    const std::string body = readTextFile(dir / "tasks.json");
    if (body.empty()) {
        return false;
    }

    const data::TaskLocale locale =
        data::TaskLocale::parse(readTextFile(dir / ("tasks_" + language_ + ".json")));
    data::TaskSet set = data::parseTasks(body, locale);
    if (set.tasks.empty()) {
        // A cache that parses to nothing is worse than none: it would sit there
        // looking valid. Drop it so the next start re-downloads.
        std::error_code ec;
        fs::remove(dir / "tasks.json", ec);
        return false;
    }

    long long writtenAt = 0;
    const std::string stamp = readTextFile(dir / "tasks.updated");
    if (!stamp.empty()) {
        writtenAt = std::strtoll(stamp.c_str(), nullptr, 10);
    }

    const std::size_t count = set.tasks.size();
    publish(std::move(set), writtenAt);
    say("quests: " + std::to_string(count) + " loaded from cache");
    return true;
}

bool TaskRepository::downloadAndCache(const std::string& mode) {
    HttpClient client;
    if (!client.open(L"tarkov-help/0.1")) {
        return false;
    }

    const std::string base = std::string(kHost) + "/" + mode;
    // The task document is a couple of megabytes; allow room but not unbounded.
    const auto tasksResponse = client.get(base + "/tasks", 32u * 1024 * 1024);
    if (!tasksResponse.ok) {
        say("quests: download failed (HTTP " +
            std::to_string(tasksResponse.status) + ")");
        return false;
    }

    const std::string body(tasksResponse.body.begin(), tasksResponse.body.end());

    // The locale is a separate document; without it every quest would be
    // titled with a hex id. Its absence is survivable, so it is not fatal.
    std::string localeBody;
    const auto localeResponse =
        client.get(base + "/tasks_" + language_, 16u * 1024 * 1024);
    if (localeResponse.ok) {
        localeBody.assign(localeResponse.body.begin(), localeResponse.body.end());
    } else {
        say("quests: no '" + language_ + "' translations, using plain names");
    }

    const data::TaskLocale locale = data::TaskLocale::parse(localeBody);
    data::TaskSet set = data::parseTasks(body, locale);
    if (set.tasks.empty()) {
        say("quests: downloaded data did not parse");
        return false;
    }

    const fs::path dir = cacheDir(mode);
    const long long writtenAt = nowUnix();
    writeTextFile(dir / "tasks.json", body);
    if (!localeBody.empty()) {
        writeTextFile(dir / ("tasks_" + language_ + ".json"), localeBody);
    }
    writeTextFile(dir / "tasks.updated", std::to_string(writtenAt));

    const std::size_t count = set.tasks.size();
    publish(std::move(set), writtenAt);
    say("quests: " + std::to_string(count) + " downloaded");
    return true;
}

void TaskRepository::run() {
    std::string loadedMode;

    while (!stopping_.load(std::memory_order_relaxed)) {
        std::string mode;
        bool wantRefresh = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            mode = wantedMode_;
            wantRefresh = refreshRequested_;
            refreshRequested_ = false;
            gameMode_ = mode;
        }

        const bool modeChanged = mode != loadedMode;
        if (modeChanged) {
            loadedMode = mode;
            if (!loadFromCache(mode)) {
                wantRefresh = true;
            }
        }

        if (!wantRefresh) {
            const auto current = status();
            if (!current.haveData ||
                (current.ageSeconds >= 0 && current.ageSeconds > kMaxAgeSeconds)) {
                wantRefresh = true;
            }
        }

        if (wantRefresh && !stopping_.load(std::memory_order_relaxed)) {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                fetching_ = true;
                detail_ = "downloading";
            }
            const bool ok = downloadAndCache(mode);
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                fetching_ = false;
                detail_ = ok ? "" : "download failed";
            }
        }

        std::unique_lock<std::mutex> lock(mutex_);
        // Wake on a mode change, an explicit refresh, or shutdown; otherwise
        // sleep long. There is nothing to poll for.
        wake_.wait_for(lock, std::chrono::minutes(30), [this] {
            return stopping_.load(std::memory_order_relaxed) || refreshRequested_ ||
                   wantedMode_ != gameMode_;
        });
    }
}

}  // namespace tarkov::win32
