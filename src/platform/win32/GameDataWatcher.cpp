#include "platform/win32/GameDataWatcher.h"

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

#include "core/game/ScreenshotName.h"

namespace tarkov::win32 {
namespace {

namespace fs = std::filesystem;

std::int64_t nowMillis() { return static_cast<std::int64_t>(GetTickCount64()); }

std::string narrow(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                         static_cast<int>(wide.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
}

/// Finds a log file in a session folder by its suffix. The names are prefixed
/// with a timestamp and version, so a suffix match is the stable way in.
std::optional<fs::path> findLogFile(const fs::path& folder, const char* suffix) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        const std::string wanted(suffix);
        if (name.size() >= wanted.size() &&
            name.compare(name.size() - wanted.size(), wanted.size(), wanted) == 0) {
            return entry.path();
        }
    }
    return std::nullopt;
}

/// An overlapped directory watch, kept armed across notifications.
class DirectoryWatch {
public:
    ~DirectoryWatch() { close(); }

    bool open(const fs::path& directory) {
        close();
        handle_ = CreateFileW(directory.c_str(), FILE_LIST_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                              nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            return false;
        }
        overlapped_ = OVERLAPPED{};
        overlapped_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        return overlapped_.hEvent != nullptr && arm();
    }

    void close() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CancelIo(handle_);
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
        if (overlapped_.hEvent != nullptr) {
            CloseHandle(overlapped_.hEvent);
            overlapped_.hEvent = nullptr;
        }
    }

    bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }
    HANDLE event() const { return overlapped_.hEvent; }

    /// Collects the names reported by the completed notification and re-arms.
    std::vector<std::wstring> collect() {
        std::vector<std::wstring> names;
        DWORD transferred = 0;
        if (!GetOverlappedResult(handle_, &overlapped_, &transferred, FALSE)) {
            arm();
            return names;
        }
        ResetEvent(overlapped_.hEvent);

        // A zero-length result means the buffer overflowed and the notification
        // was coalesced. The caller has to rescan; report nothing rather than
        // pretend we know what changed.
        if (transferred != 0) {
            const std::byte* cursor = buffer_.data();
            for (;;) {
                const auto* info =
                    reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(cursor);
                if (info->Action == FILE_ACTION_ADDED ||
                    info->Action == FILE_ACTION_RENAMED_NEW_NAME ||
                    info->Action == FILE_ACTION_MODIFIED) {
                    names.emplace_back(info->FileName,
                                       info->FileNameLength / sizeof(wchar_t));
                }
                if (info->NextEntryOffset == 0) {
                    break;
                }
                cursor += info->NextEntryOffset;
            }
        }
        arm();
        return names;
    }

private:
    bool arm() {
        if (handle_ == INVALID_HANDLE_VALUE) {
            return false;
        }
        ResetEvent(overlapped_.hEvent);
        return ReadDirectoryChangesW(
                   handle_, buffer_.data(), static_cast<DWORD>(buffer_.size()), FALSE,
                   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                       FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
                   nullptr, &overlapped_, nullptr) != 0;
    }

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped_{};
    // Generous: the screenshots folder can receive a burst, and an overflow
    // costs us the detail of what changed.
    alignas(DWORD) std::array<std::byte, 64 * 1024> buffer_{};
};

}  // namespace

GameDataWatcher::~GameDataWatcher() { stop(); }

void GameDataWatcher::say(const std::string& message) const {
    if (logger_) {
        logger_(message);
    }
}

bool GameDataWatcher::start(game::RaidState& state, Logger logger,
                            data::QuestProgressStore* progress,
                            ProgressChanged onProgress) {
    logger_ = std::move(logger);
    progress_ = progress;
    onProgress_ = std::move(onProgress);

    auto discovered = discoverGamePaths();
    if (!discovered) {
        say("could not find the game's folders; is Escape from Tarkov installed?");
        return false;
    }
    paths_ = *discovered;

    if (paths_.logsRoot.empty()) {
        say("no log folder found -- map detection will not work");
    }
    if (paths_.screenshots.empty()) {
        say("no screenshots folder found -- position tracking will not work");
    }

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEvent_ == nullptr) {
        return false;
    }

    stopping_.store(false, std::memory_order_relaxed);
    thread_ = std::thread(&GameDataWatcher::run, this, &state);
    return true;
}

void GameDataWatcher::stop() {
    if (!thread_.joinable()) {
        return;
    }
    stopping_.store(true, std::memory_order_relaxed);
    if (stopEvent_ != nullptr) {
        SetEvent(stopEvent_);
    }
    thread_.join();
    if (stopEvent_ != nullptr) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
}

void GameDataWatcher::openNewestLogFolder() {
    if (paths_.logsRoot.empty()) {
        return;
    }
    auto folder = newestLogFolder(paths_.logsRoot);
    if (!folder || *folder == currentLogFolder_) {
        return;
    }

    currentLogFolder_ = *folder;
    applicationLog_.close();
    outputLog_.close();
    notificationsLog_.close();
    applicationParser_ = game::LogParser{};
    outputParser_ = game::LogParser{};
    notificationsParser_ = game::QuestEventParser{};

    if (auto path = findLogFile(currentLogFolder_, "application_000.log")) {
        applicationLog_.open(*path);
    }
    if (auto path = findLogFile(currentLogFolder_, "output_000.log")) {
        outputLog_.open(*path);
    }
    if (progress_ != nullptr) {
        if (auto path = findLogFile(currentLogFolder_, "push-notifications_000.log")) {
            notificationsLog_.open(*path);
        }
    }

    say("watching log session " + currentLogFolder_.filename().string());
}

void GameDataWatcher::pumpLogs(game::RaidState& state) {
    struct Source {
        LogTail& tail;
        game::LogParser& parser;
    };
    Source sources[] = {{applicationLog_, applicationParser_},
                        {outputLog_, outputParser_}};

    for (auto& source : sources) {
        if (!source.tail.isOpen()) {
            continue;
        }
        for (const auto& line : source.tail.readNewLines()) {
            if (auto event = source.parser.feedLine(line)) {
                state.apply(*event, nowMillis());
                if (event->type == game::RaidEventType::RaidCreated) {
                    say("raid on " + (event->mapId.empty() ? std::string("?")
                                                           : event->mapId));
                }
            }
        }
    }
}

void GameDataWatcher::importQuestHistory() {
    if (progress_ == nullptr || paths_.logsRoot.empty()) {
        return;
    }
    std::error_code ec;
    std::vector<fs::path> folders;
    for (const auto& entry : fs::directory_iterator(paths_.logsRoot, ec)) {
        if (entry.is_directory(ec) &&
            entry.path().filename().string().rfind("log_", 0) == 0) {
            folders.push_back(entry.path());
        }
    }
    // Oldest first. Order does not affect the result -- events are merged by
    // their own timestamps -- but it keeps the log of what happened readable.
    std::sort(folders.begin(), folders.end());

    int events = 0;
    for (const auto& folder : folders) {
        const auto path = findLogFile(folder, "push-notifications_000.log");
        if (!path) {
            continue;
        }
        LogTail history;
        if (!history.open(*path, /*fromStart=*/true)) {
            continue;
        }
        game::QuestEventParser parser;
        for (const auto& line : history.readNewLines()) {
            if (auto event = parser.feedLine(line)) {
                progress_->apply(*event);
                ++events;
            }
        }
    }
    if (events > 0) {
        say("quests: " + std::to_string(events) + " events read from " +
            std::to_string(folders.size()) + " log sessions");
        if (onProgress_) {
            onProgress_();
        }
    }
}

void GameDataWatcher::pumpNotifications() {
    if (progress_ == nullptr || !notificationsLog_.isOpen()) {
        return;
    }
    bool changed = false;
    for (const auto& line : notificationsLog_.readNewLines()) {
        if (auto event = notificationsParser_.feedLine(line)) {
            changed = progress_->apply(*event) || changed;
        }
    }
    if (changed && onProgress_) {
        onProgress_();
    }
}

void GameDataWatcher::handleScreenshotNames(game::RaidState& state,
                                            const std::vector<std::wstring>& names) {
    for (const auto& wide : names) {
        const std::string name = narrow(wide);
        if (auto shot = game::parseScreenshotName(name)) {
            state.applyPosition(*shot, nowMillis());
        }
    }
}

void GameDataWatcher::run(game::RaidState* state) {
    DirectoryWatch logsWatch;
    DirectoryWatch shotsWatch;

    if (!paths_.logsRoot.empty()) {
        // Watch the root, not one session folder: a new folder appears every
        // time the game is launched.
        logsWatch.open(paths_.logsRoot);
    }
    if (!paths_.screenshots.empty()) {
        shotsWatch.open(paths_.screenshots);
    }

    importQuestHistory();
    openNewestLogFolder();

    while (!stopping_.load(std::memory_order_relaxed)) {
        HANDLE handles[3];
        DWORD count = 0;
        handles[count++] = stopEvent_;
        const DWORD logsIndex = logsWatch.valid() ? count : 0xFFFFFFFF;
        if (logsWatch.valid()) {
            handles[count++] = logsWatch.event();
        }
        const DWORD shotsIndex = shotsWatch.valid() ? count : 0xFFFFFFFF;
        if (shotsWatch.valid()) {
            handles[count++] = shotsWatch.event();
        }

        // The timeout is a safety net, not the mechanism: it re-checks for a
        // new log session and covers a coalesced notification. When nothing
        // happens, this thread sleeps.
        const DWORD result = WaitForMultipleObjects(count, handles, FALSE, 1000);

        if (result == WAIT_OBJECT_0) {
            break;  // stop requested
        }

        if (result == WAIT_TIMEOUT) {
            openNewestLogFolder();
            pumpLogs(*state);
            pumpNotifications();
            continue;
        }

        if (logsIndex != 0xFFFFFFFF && result == WAIT_OBJECT_0 + logsIndex) {
            logsWatch.collect();
            openNewestLogFolder();
            pumpLogs(*state);
            pumpNotifications();
        } else if (shotsIndex != 0xFFFFFFFF && result == WAIT_OBJECT_0 + shotsIndex) {
            handleScreenshotNames(*state, shotsWatch.collect());
        }
    }

    applicationLog_.close();
    outputLog_.close();
    notificationsLog_.close();
}

}  // namespace tarkov::win32
