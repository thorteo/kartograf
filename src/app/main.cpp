// tarkov-help -- entry point.
//
// Wires the pieces together: a watcher thread reads the files the game writes
// and is the sole writer to RaidState; the render thread takes an immutable
// snapshot once per frame and draws the HUD into a DirectComposition surface
// layered over the game.
//
// The map view comes next; see docs/features/roadmap.md.

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "core/data/EmbeddedMaps.h"
#include "core/data/MapIds.h"
#include "core/data/QuestProgress.h"
#include "core/data/TaskModel.h"
#include "core/game/RaidState.h"
#include "core/game/ScreenshotName.h"
#include "core/geo/MapConfig.h"
#include "core/input/Hotkey.h"
#include "core/settings/Settings.h"
#include "platform/win32/GameDataWatcher.h"
#include "platform/win32/GameWindow.h"
#include "platform/win32/ImGuiLayer.h"
#include "platform/win32/OverlayWindow.h"
#include "core/map/SvgPlacement.h"
#include "platform/win32/IconStore.h"
#include "platform/win32/SettingsWindow.h"
#include "platform/win32/SvgMapStore.h"
#include "platform/win32/TarkovDataRepository.h"
#include "platform/win32/TileStore.h"
#include "ui/Hud.h"
#include "ui/MapView.h"
#include "ui/QuestGlyphs.h"
#include "ui/QuestList.h"
#include "ui/QuestNeeds.h"
#include "ui/SettingsView.h"
#include "ui/Strings.h"

namespace {

constexpr int kMapHotkeyId = 1;
constexpr int kInteractHotkeyId = 2;
constexpr int kHudHotkeyId = 3;

/// Status lines, shown briefly in the overlay so the user is not expected to
/// have a console open.
///
/// Written from the watcher thread and read from the render thread, so it
/// carries its own lock. It is the one piece of shared state outside
/// RaidState, and it is deliberately tiny and self-contained.
class StatusLog {
public:
    void add(const std::string& message) {
        const std::lock_guard<std::mutex> lock(mutex_);
        lines_.push_back({message, GetTickCount64()});
        while (lines_.size() > 6) {
            lines_.pop_front();
        }
    }

    template <typename Fn>
    void forEachRecent(unsigned long long nowMillis, Fn&& fn) const {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& line : lines_) {
            if (nowMillis - line.at < 12000ULL) {
                fn(line.text);
            }
        }
    }

private:
    struct Line {
        std::string text;
        unsigned long long at;
    };
    mutable std::mutex mutex_;
    std::deque<Line> lines_;
};

StatusLog g_status;

tarkov::input::Hotkey resolveHotkey(const std::string& text,
                                    tarkov::input::Hotkey fallback) {
    using namespace tarkov::input;
    if (text.empty()) {
        return fallback;
    }
    const auto parsed = parseHotkey(text);
    if (!parsed) {
        g_status.add(tarkov::ui::trf(tarkov::ui::Str::StatusCouldNotParseKey, text.c_str(),
                                     formatHotkey(fallback).c_str()));
        return fallback;
    }
    if (isReservedCombination(*parsed)) {
        g_status.add(tarkov::ui::trf(tarkov::ui::Str::StatusReservedKey, text.c_str(),
                                     formatHotkey(fallback).c_str()));
        return fallback;
    }
    return *parsed;
}

std::string argumentValue(int argc, char** argv, const char* name) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == name) {
            return argv[i + 1];
        }
    }
    return {};
}

bool hasFlag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == name) {
            return true;
        }
    }
    return false;
}

std::filesystem::path settingsFile() {
    return tarkov::win32::TarkovDataRepository::defaultCacheRoot().parent_path() /
           "settings.json";
}

tarkov::settings::Settings loadSettings() {
    std::ifstream in(settingsFile(), std::ios::binary);
    std::ostringstream buffer;
    if (in) {
        buffer << in.rdbuf();
    }
    auto loaded = tarkov::settings::parse(buffer.str());
    tarkov::settings::sanitize(loaded);
    return loaded;
}

void saveSettings(const tarkov::settings::Settings& settings) {
    const std::string text = tarkov::settings::serialize(settings);
    const auto path = settingsFile();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto temporary = path.string() + ".part";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            return;
        }
    }
    std::filesystem::rename(temporary, path, ec);
}

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(length > 0 ? length - 1 : 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), length);
    return out;
}

/// Registers one hotkey. Returns an empty string on success, or the problem
/// to show beside that key in the settings window.
std::string registerOrReport(int id, const tarkov::input::Hotkey& hotkey,
                             const char* purpose) {
    if (!hotkey.valid()) {
        return {};
    }
    if (RegisterHotKey(nullptr, id, hotkey.modifiers | MOD_NOREPEAT,
                       hotkey.virtualKey)) {
        g_status.add(tarkov::ui::trf(tarkov::ui::Str::StatusHotkeyBound, purpose,
                                     tarkov::input::formatHotkey(hotkey).c_str()));
        return {};
    }
    const DWORD error = GetLastError();
    if (error == ERROR_HOTKEY_ALREADY_REGISTERED) {
        g_status.add(tarkov::ui::trf(tarkov::ui::Str::StatusHotkeyClaimed,
                                     tarkov::input::formatHotkey(hotkey).c_str()));
        return tarkov::ui::tr(tarkov::ui::Str::KeyClaimed);
    }
    g_status.add(tarkov::ui::trf(tarkov::ui::Str::StatusHotkeyFailed,
                                 tarkov::input::formatHotkey(hotkey).c_str()));
    return tarkov::ui::trf(tarkov::ui::Str::StatusHotkeyFailed,
                           tarkov::input::formatHotkey(hotkey).c_str());
}

void unregisterHotkeys() {
    UnregisterHotKey(nullptr, kMapHotkeyId);
    UnregisterHotKey(nullptr, kInteractHotkeyId);
    UnregisterHotKey(nullptr, kHudHotkeyId);
}

void drawStatus(unsigned long long nowMillis) {
    bool any = false;
    g_status.forEachRecent(nowMillis, [&](const std::string&) { any = true; });
    if (!any) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 16.0f,
                                   viewport->WorkPos.y + viewport->WorkSize.y - 16.0f),
                            ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.70f);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("tarkov-help-status", nullptr, flags)) {
        g_status.forEachRecent(nowMillis, [](const std::string& text) {
            ImGui::TextColored(ImVec4(0.62f, 0.64f, 0.58f, 1.0f), "%s", text.c_str());
        });
    }
    ImGui::End();
}

std::filesystem::path progressFile() {
    return tarkov::win32::TarkovDataRepository::defaultCacheRoot().parent_path() /
           "progress.json";
}

std::mutex g_progressFileMutex;

/// Writes progress beside the rest of the cache. Called from the watcher
/// thread after log events and from the UI thread after a manual correction,
/// hence the lock; the temporary-then-rename keeps a crash from truncating it.
void saveProgress(const tarkov::data::QuestProgressStore& store) {
    const std::lock_guard<std::mutex> lock(g_progressFileMutex);
    const std::string text = store.snapshot()->serialize();
    const auto path = progressFile();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto temporary = path.string() + ".part";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            return;
        }
    }
    std::filesystem::rename(temporary, path, ec);
}

tarkov::data::QuestProgress loadProgress() {
    std::ifstream in(progressFile(), std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return tarkov::data::QuestProgress::parse(buffer.str());
}

/// Builds the markers for one map out of the quest data.
///
/// A task can reference a map through several ids -- night Factory and Factory
/// share a picture -- so every id that draws as this map is collected, or
/// quests would vanish depending on which variant the raid used.
std::vector<tarkov::ui::QuestPlace> buildQuestPlaces(
    const tarkov::data::TaskSet& tasks, const tarkov::data::MapIdTable& mapIds,
    const std::string& drawnAsName,
    const tarkov::data::EffectiveProgress* activeOnly) {
    using namespace tarkov;

    std::vector<ui::QuestPlace> places;
    if (drawnAsName.empty()) {
        return places;
    }
    const auto ids = mapIds.idsFor(drawnAsName);
    if (ids.empty()) {
        return places;
    }
    const auto belongs = [&](const std::string& mapId) {
        return std::find(ids.begin(), ids.end(), mapId) != ids.end();
    };

    for (const auto& task : tasks.tasks) {
        // Only the player's current quests, when progress is known. Every
        // quest in the game on one map is 150+ markers -- noise, not help.
        if (activeOnly != nullptr &&
            activeOnly->stateOf(task.id) != tarkov::data::QuestState::Active) {
            continue;
        }
        const std::string name = task.displayName();
        // What the quest wants handed over, shown at every one of its places.
        const auto handIn = ui::handInLines(task, tasks);
        for (const auto& objective : task.objectives) {
            auto needs = ui::objectiveBringLines(objective, tasks);
            needs.insert(needs.end(), handIn.begin(), handIn.end());
            for (const auto& zone : objective.zones) {
                if (!belongs(zone.mapId)) {
                    continue;
                }
                ui::QuestPlace place;
                place.world = zone.position;
                place.taskName = name;
                place.detail = objective.description;
                place.optional = objective.optional;
                place.kind = data::objectiveKind(objective.type);
                place.needs = needs;
                place.top = zone.top;
                place.bottom = zone.bottom;
                places.push_back(std::move(place));
            }
            for (const auto& location : objective.questItemLocations) {
                if (!belongs(location.mapId)) {
                    continue;
                }
                for (const auto& position : location.positions) {
                    ui::QuestPlace place;
                    place.world = position;
                    place.taskName = name;
                    place.detail = objective.description;
                    place.optional = objective.optional;
                    place.kind = data::objectiveKind(objective.type);
                    place.needs = needs;
                    place.isQuestItem = true;
                    places.push_back(std::move(place));
                }
            }
        }
    }
    return places;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace tarkov;

    // Settings first, before anything can report a status line, since the
    // language is one of them. Command-line flags are development aids and
    // override the file for this run.
    settings::Settings config = loadSettings();
    if (const auto value = argumentValue(argc, argv, "--lang"); !value.empty()) {
        config.language = value;
    }
    config.mapKey = resolveHotkey(argumentValue(argc, argv, "--toggle-key"), config.mapKey);
    config.interactKey =
        resolveHotkey(argumentValue(argc, argv, "--interact-key"), config.interactKey);
    config.hudKey = resolveHotkey(argumentValue(argc, argv, "--hud-key"), config.hudKey);
    if (const auto value = argumentValue(argc, argv, "--map-opacity"); !value.empty()) {
        config.mapOpacity = static_cast<float>(std::atof(value.c_str()));
    }
    if (hasFlag(argc, argv, "--hud")) {
        config.showHud = true;
    }
    if (hasFlag(argc, argv, "--allow-capture")) {
        config.excludeFromCapture = false;
    }
    settings::sanitize(config);
    ui::setLanguage(ui::parseLanguage(config.language));
    const bool debug = hasFlag(argc, argv, "--debug");
    // Development and first-run aids: show the overlay with no game running,
    // and allow it into screen captures so it can be photographed.
    const bool standalone = hasFlag(argc, argv, "--no-game");
    // Development aids: open a named map with no raid running, and inject a
    // position by pasting a real screenshot filename. Together these let the
    // marker be checked against the map without playing.
    const std::string forcedMap = argumentValue(argc, argv, "--map");
    const std::string fakeShot = argumentValue(argc, argv, "--fake-shot");
    // Whether to open the settings window now. --no-settings keeps scripted
    // runs from stopping at it; --settings forces it.
    const bool openSettingsAtStart =
        hasFlag(argc, argv, "--settings") ||
        (config.showSettingsOnStart && !hasFlag(argc, argv, "--no-settings"));
    const int runSeconds = [&] {
        const std::string value = argumentValue(argc, argv, "--run-seconds");
        return value.empty() ? 0 : std::atoi(value.c_str());
    }();

    HINSTANCE instance = GetModuleHandleW(nullptr);

    win32::OverlayWindow overlay;
    if (!overlay.create(instance, 1920, 1080)) {
        MessageBoxA(nullptr, overlay.lastError().c_str(), "tarkov-help",
                    MB_ICONERROR | MB_OK);
        return 1;
    }

    win32::ImGuiLayer imgui;
    overlay.setUiLayer(&imgui);
    if (!imgui.initialise(overlay.handle(), overlay.device(), overlay.context())) {
        // MessageBoxW: the text may be Russian, which the ANSI variant would
        // mangle through the system code page.
        const char* message = ui::tr(ui::Str::ErrorInterfaceInit);
        const int length = MultiByteToWideChar(CP_UTF8, 0, message, -1, nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(length > 0 ? length : 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, message, -1, wide.data(), length);
        MessageBoxW(nullptr, wide.c_str(), L"tarkov-help", MB_ICONERROR | MB_OK);
        return 1;
    }

    const auto applyCaptureExclusion = [&overlay](bool exclude) {
        switch (overlay.setExcludedFromCapture(exclude)) {
            case win32::OverlayWindow::CaptureExclusion::kHidden:
                g_status.add(ui::tr(ui::Str::StatusCaptureHidden));
                break;
            case win32::OverlayWindow::CaptureExclusion::kBlanked:
                g_status.add(ui::tr(ui::Str::StatusCaptureBlanked));
                break;
            case win32::OverlayWindow::CaptureExclusion::kUnavailable:
                g_status.add(exclude ? ui::tr(ui::Str::StatusCaptureFailed)
                                     : ui::tr(ui::Str::StatusCaptureVisible));
                break;
        }
    };
    applyCaptureExclusion(config.excludeFromCapture);

    ui::SettingsViewState settingsView;
    // Re-registers every hotkey from the current settings and records, per
    // key, why one could not be bound, for the settings window to show.
    const auto applyHotkeys = [&config, &settingsView] {
        unregisterHotkeys();
        settingsView.hotkeyErrors[0] =
            registerOrReport(kMapHotkeyId, config.mapKey, ui::tr(ui::Str::HotkeyPurposeMap));
        settingsView.hotkeyErrors[1] = registerOrReport(
            kInteractHotkeyId, config.interactKey, ui::tr(ui::Str::HotkeyPurposeInteract));
        settingsView.hotkeyErrors[2] =
            registerOrReport(kHudHotkeyId, config.hudKey, ui::tr(ui::Str::HotkeyPurposeHud));
    };
    applyHotkeys();

    win32::SettingsWindow settingsWindow;
    const auto trayTooltip = [&config] {
        return widen(ui::trf(ui::Str::TrayRunning,
                             input::formatHotkey(config.mapKey).c_str()));
    };
    settingsWindow.create(instance, overlay.device(), overlay.context(),
                          widen(ui::tr(ui::Str::SettingsTitle)), trayTooltip());
    settingsWindow.setTrayMenuLabels(widen(ui::tr(ui::Str::TraySettings)),
                                     widen(ui::tr(ui::Str::TrayQuit)));
    if (openSettingsAtStart) {
        settingsWindow.show();
    }
    bool capturingKey = false;
    bool toldAboutTray = false;

    const std::vector<geo::MapDefinition> maps = geo::parseMapsJson(data::kMapsJson);
    if (maps.empty()) {
        g_status.add(ui::tr(ui::Str::StatusMapDataBroken));
    }

    win32::TileStore tiles;
    tiles.start(overlay.device());

    win32::IconStore icons;
    icons.start(overlay.device(),
                {"quest_objective", "extract_pmc",
                 "extract_scav", "extract_shared", "extract_transit", "spawn_pmc",
                 "spawn_boss"});

    // Quest glyphs are ours, not downloaded: rasterise them now, at twice the
    // size they are drawn so they stay sharp.
    for (const auto kind : {data::ObjectiveKind::Visit, data::ObjectiveKind::Mark,
                            data::ObjectiveKind::Plant, data::ObjectiveKind::Find,
                            data::ObjectiveKind::Shoot, data::ObjectiveKind::Use}) {
        icons.add(ui::questGlyphName(kind), ui::renderQuestGlyph(kind, 32));
    }

    win32::SvgMapStore abstractMaps;
    abstractMaps.start(overlay.device(),
                       [](const std::string& line) { g_status.add(line); });

    const data::MapIdTable mapIds = data::MapIdTable::parse(data::kMapIdsJson);
    win32::TarkovDataRepository quests;
    quests.start("regular", config.language,
                 [](const std::string& line) { g_status.add(line); });

    data::QuestProgressStore progress;
    progress.replace(loadProgress());

    game::RaidState raidState;
    if (!forcedMap.empty()) {
        game::RaidEvent forced;
        forced.type = game::RaidEventType::MapLoading;
        forced.mapId = forcedMap;
        raidState.apply(forced, static_cast<std::int64_t>(GetTickCount64()));
        g_status.add(ui::trf(ui::Str::StatusForcedMap, forcedMap.c_str()));
    }
    if (!fakeShot.empty()) {
        if (const auto shot = game::parseScreenshotName(fakeShot)) {
            raidState.applyPosition(*shot,
                                    static_cast<std::int64_t>(GetTickCount64()));
            g_status.add(ui::tr(ui::Str::StatusInjectedPosition));
        } else {
            g_status.add(ui::tr(ui::Str::StatusInjectedPositionBad));
        }
    }

    win32::GameDataWatcher watcher;
    if (!watcher.start(
            raidState, [](const std::string& line) { g_status.add(line); }, &progress,
            [&progress] { saveProgress(progress); })) {
        g_status.add(ui::tr(ui::Str::StatusNoGameData));
    }

    win32::GameWindowTracker tracker;
    bool overlayEnabled = true;
    // Development aid: start clickable, so the quest panel can be seen without
    // pressing the hotkey.
    bool interactive = hasFlag(argc, argv, "--interactive");
    if (interactive) {
        overlay.setClickThrough(false);
        overlay.setInteractive(true);
    }
    bool warnedAboutFullscreen = false;

    ui::HudOptions hudOptions;
    hudOptions.showDebug = debug;

    ui::MapViewState mapState;
    // The map's options come from the settings each frame; the map panel in
    // game keeps only the floor choice, which is a moment-to-moment decision.
    const auto applyMapSettings = [&config](ui::MapViewState& state) {
        state.opacity = config.mapOpacity;
        state.preferAbstract = config.preferAbstractMap;
        state.showQuests = config.showQuests;
        state.showExtracts = config.showExtracts;
        state.showTransits = config.showTransits;
        state.showSpawns = config.showSpawns;
        state.dimOtherFloors = config.dimOtherFloors;
    };
    applyMapSettings(mapState);
    // Asking for a map on the command line means you want to see it.
    bool mapVisible = !forcedMap.empty();
    // Off by default. Almost everything it showed is either visible on the map,
    // shown by the game itself, or a raw coordinate no one navigates by; what
    // was actually unique -- the age of the position fix -- now lives in the
    // map footer. Screen corners in a raid are worth more than telemetry.
    bool hudVisible = config.showHud;
    std::string tiledMap;
    std::string abstractMapKey;
    int abstractMapLayer = -2;
    // Rebuilt only when the map or the quest data changes; walking through a
    // raid must not re-scan 515 tasks every frame.
    std::vector<ui::QuestPlace> questPlaces;
    std::string questPlacesFor;
    const data::TaskSet* questPlacesFrom = nullptr;
    std::uint64_t questPlacesRevision = ~0ULL;
    bool questPlacesFiltered = false;
    int activeQuestCount = 0;
    std::vector<ui::QuestListEntry> questEntries;
    ui::MapOverlays overlaysForMap;
    const void* overlaysFeaturesFrom = nullptr;
    std::string lastRaidMode;

    std::shared_ptr<const game::RaidSnapshot> lastSnapshot;
    std::optional<POINT> lastPointer;
    unsigned long long lastDisplaySecond = 0;
    // Set whenever something outside the snapshot changes what is on screen.
    bool forceRedraw = true;
    // Part of the map is still on its way. Not a reason to draw -- each piece
    // arrives through a store's pump, which asks for the frame itself -- only
    // to poll the pumps sooner, so tiles appear as they land.
    bool mapLoading = false;

    const ULONGLONG startedAt = GetTickCount64();
    if (standalone) {
        g_status.add(ui::tr(ui::Str::StatusStandalone));
    }

    // Quest counts for the settings window's status section, recomputed only
    // when progress or quest data change.
    std::uint64_t statusRevision = ~0ULL;
    const data::TaskSet* statusTasks = nullptr;
    int statusActive = 0;

    while (overlay.pumpMessages()) {
        // --- Settings window and tray --------------------------------------
        if (settingsWindow.takeQuitRequest()) {
            break;
        }
        if (settingsWindow.takeOpenRequest()) {
            settingsWindow.show();
        }
        if (settingsWindow.takeClosedByUser() && !toldAboutTray) {
            // The first time the window is closed, say where the program went;
            // otherwise closing it looks like quitting.
            toldAboutTray = true;
            settingsWindow.notify(L"tarkov-help", trayTooltip());
        }

        if (settingsWindow.visible()) {
            // Hand key capture over to the window. Global hotkeys are released
            // while it listens, or pressing a bound key (say F5) would fire the
            // hotkey instead of reaching the capture.
            if (settingsView.capturing && !capturingKey) {
                capturingKey = true;
                unregisterHotkeys();
                settingsWindow.startKeyCapture();
            }
            if (capturingKey) {
                if (auto key = settingsWindow.takeCapturedKey()) {
                    settingsView.captured = *key;
                }
            }

            const auto questData = quests.tasks();
            const std::uint64_t revision = progress.revision();
            if (statusRevision != revision || statusTasks != questData.get()) {
                statusRevision = revision;
                statusTasks = questData.get();
                statusActive = static_cast<int>(
                    data::resolveProgress(*progress.snapshot(), *questData).active.size());
            }
            ui::SettingsStatus status;
            status.gameRunning = tracker.poll().found;
            status.logFolderFound = !watcher.paths().logsRoot.empty();
            status.screenshotFolderFound = !watcher.paths().screenshots.empty();
            status.questsLoaded = static_cast<int>(questData->tasks.size());
            status.questsActive = statusActive;
            status.questDataLoading = quests.status().fetching;
            const auto raid = raidState.snapshot();
            status.positionAgeSeconds =
                raid->fix.valid
                    ? raid->fix.ageSeconds(static_cast<std::int64_t>(GetTickCount64()))
                    : -1.0;

            const settings::Settings before = config;
            ui::SettingsViewResult outcome;
            settingsWindow.render(
                [&] { outcome = ui::drawSettings(config, settingsView, status); });

            if (outcome.changed) {
                settings::sanitize(config);
                saveSettings(config);
                // Apply everything that changed, immediately.
                if (config.language != before.language) {
                    ui::setLanguage(ui::parseLanguage(config.language));
                    quests.setLanguage(config.language);
                    settingsWindow.setTrayMenuLabels(widen(ui::tr(ui::Str::TraySettings)),
                                                     widen(ui::tr(ui::Str::TrayQuit)));
                    questPlacesRevision = ~0ULL;  // rebuild names
                    overlaysFeaturesFrom = nullptr;
                }
                if (config.excludeFromCapture != before.excludeFromCapture) {
                    applyCaptureExclusion(config.excludeFromCapture);
                }
                if (config.showHud != before.showHud) {
                    hudVisible = config.showHud;
                }
                applyMapSettings(mapState);
                forceRedraw = true;
            }
            if (outcome.done) {
                settingsWindow.hide();
            }
            if (outcome.quit) {
                break;
            }
        }

        // Capture ends when the view has its key, or when the window went away
        // mid-capture -- closed, or "Done" -- which must not leave every hotkey
        // unregistered, and the view waiting for a key it can no longer hear.
        if (capturingKey && (!settingsView.capturing || !settingsWindow.visible())) {
            capturingKey = false;
            settingsView.capturing.reset();
            settingsView.captured.reset();
            settingsWindow.stopKeyCapture();
            applyHotkeys();  // back on, with whatever was chosen
        }

        MSG message;
        while (PeekMessageW(&message, nullptr, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                // Retrieved whatever the filter says; hand it back to the pump.
                PostQuitMessage(static_cast<int>(message.wParam));
                break;
            }
            if (message.message != WM_HOTKEY) {
                continue;
            }
            if (message.wParam == kMapHotkeyId) {
                mapVisible = !mapVisible;
                if (mapVisible) {
                    // Opening the map is when the player looks for themselves.
                    mapState.pulseFromMillis = static_cast<std::int64_t>(GetTickCount64());
                }
                forceRedraw = true;
            } else if (message.wParam == kHudHotkeyId) {
                hudVisible = !hudVisible;
                forceRedraw = true;
            } else if (message.wParam == kInteractHotkeyId) {
                forceRedraw = true;
                interactive = !interactive;
                // Click-through is the default state: the overlay must not
                // swallow clicks meant for the game.
                overlay.setClickThrough(!interactive);
                overlay.setInteractive(interactive);
                g_status.add(interactive ? ui::tr(ui::Str::StatusInteractive)
                                         : ui::tr(ui::Str::StatusClickThrough));
            }
        }

        if (runSeconds > 0 &&
            GetTickCount64() - startedAt > static_cast<ULONGLONG>(runSeconds) * 1000) {
            break;
        }

        auto game = tracker.poll();
        if (standalone && !game.found) {
            // Anchor to the primary monitor so the overlay can be seen and
            // photographed without the game running.
            game.found = true;
            // With no game, "the game is in front" means nothing else is; the
            // settings window in front would otherwise be covered by the map.
            game.foreground = GetForegroundWindow() != settingsWindow.handle();
            game.bounds = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN),
                               GetSystemMetrics(SM_CYSCREEN)};
        }
        const bool shouldShow = overlayEnabled && game.found && game.foreground;
        overlay.setVisible(shouldShow);

        if (!shouldShow) {
            // Nothing to composite. Idle cheaply rather than spinning -- but
            // wake promptly for input, and keep the settings window smooth.
            MsgWaitForMultipleObjects(0, nullptr, FALSE,
                                      settingsWindow.visible() ? 16 : 60, QS_ALLINPUT);
            continue;
        }

        const int width = game.bounds.right - game.bounds.left;
        const int height = game.bounds.bottom - game.bounds.top;
        if (width > 0 && height > 0) {
            if (width != overlay.width() || height != overlay.height()) {
                // The swap chain was recreated; the old contents are gone.
                forceRedraw = true;
            }
            overlay.setBounds(game.bounds.left, game.bounds.top, width, height);
        }

        if (!warnedAboutFullscreen &&
            win32::GameWindowTracker::looksLikeExclusiveFullscreen(
                GetForegroundWindow())) {
            warnedAboutFullscreen = true;
            g_status.add(ui::tr(ui::Str::StatusSwitchToBorderless));
        }

        const auto snapshot = raidState.snapshot();
        const auto nowTicks = GetTickCount64();

        // The game's own logs say whether this is PvE, so the right dataset can
        // be chosen without asking.
        if (snapshot->raidMode != lastRaidMode) {
            lastRaidMode = snapshot->raidMode;
            if (!lastRaidMode.empty()) {
                quests.setGameMode(lastRaidMode == "Online" ? "regular" : "pve");
            }
        }

        // Reactive rendering.
        //
        // The overlay is static almost all of the time: a position arrives only
        // when the player presses the screenshot key, and the only thing that
        // changes by itself is the second on the raid timer. Presenting every
        // vsync to redraw an unchanged panel would take frames from the game
        // for nothing, which is exactly the cost this project set out to avoid.
        //
        // So draw when the state object actually changed, when a displayed
        // second ticked over, or while the user is interacting -- and sleep
        // otherwise.
        const unsigned long long displaySecond = nowTicks / 1000;
        const bool stateChanged = snapshot.get() != lastSnapshot.get();
        const bool secondTicked = displaySecond != lastDisplaySecond;
        // A tile arriving changes the picture even though no state did, so the
        // upload count is part of what makes a frame necessary.
        const int uploaded = tiles.pump();
        const bool abstractUploaded = abstractMaps.pump();
        const bool iconsUploaded = icons.pump();
        // Hovering a marker while the overlay is click-through. The game's
        // pointer is followed only while the map is open and the game has let
        // go of the cursor, and a frame is drawn only when it moves.
        std::optional<POINT> pointer;
        if (mapVisible && !interactive) {
            pointer = win32::usablePointer(game.bounds);
        }
        const bool pointerMoved =
            pointer.has_value() != lastPointer.has_value() ||
            (pointer && (pointer->x != lastPointer->x || pointer->y != lastPointer->y));

        const bool needsFrame = interactive || stateChanged || secondTicked ||
                                forceRedraw || uploaded > 0 || abstractUploaded ||
                                iconsUploaded || pointerMoved;

        if (!needsFrame) {
            // Wake early if a message arrives, so input stays responsive. The
            // game's pointer sends us no messages, so poll it a little faster
            // while it is free over the map, and faster still while map
            // pieces are landing.
            MsgWaitForMultipleObjects(0, nullptr, FALSE,
                                      settingsWindow.visible() || mapLoading ? 16
                                      : pointer                          ? 33
                                                                         : 100,
                                      QS_ALLINPUT);
            continue;
        }
        lastPointer = pointer;
        if (!interactive) {
            imgui.setPassivePointer(pointer);
        }

        lastSnapshot = snapshot;
        lastDisplaySecond = displaySecond;
        forceRedraw = false;

        // Fully transparent: only what we draw is visible over the game.
        overlay.beginFrame(0.0f, 0.0f, 0.0f, 0.0f);
        imgui.beginFrame(overlay.width(), overlay.height());

        mapLoading = false;
        // The map goes underneath, so the HUD and status stay legible over it.
        if (mapVisible && !maps.empty()) {
            const geo::MapVariant* variant = nullptr;
            if (!snapshot->mapNormalized.empty()) {
                if (const auto* definition =
                        geo::findMap(maps, snapshot->mapNormalized)) {
                    variant = definition->interactiveVariant();
                }
            }

            if (variant == nullptr) {
                ImGui::SetNextWindowPos(
                    ImVec2(overlay.width() / 2.0f, overlay.height() / 2.0f),
                    ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                if (ImGui::Begin("tarkov-help-nomap", nullptr,
                                 ImGuiWindowFlags_NoDecoration |
                                     ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoNav |
                                     ImGuiWindowFlags_NoMove)) {
                    ImGui::TextUnformatted(ui::tr(ui::Str::WaitingForRaid));
                    ImGui::TextDisabled("%s", ui::tr(ui::Str::WaitingForRaidDetail));
                }
                ImGui::End();
            } else {
                // Point the abstract store at this map and floor. The layer
                // set changes as the player walks upstairs, which re-renders.
                if (abstractMapKey != variant->key ||
                    abstractMapLayer != mapState.activeLayer) {
                    abstractMapKey = variant->key;
                    abstractMapLayer = mapState.activeLayer;
                    abstractMaps.setMap(
                        variant->key, variant->svgPath,
                        map::svgLayerIdsFor(*variant, mapState.activeLayer));
                }

                if (tiledMap != variant->key) {
                    tiledMap = variant->key;
                    std::vector<std::string> layerTemplates;
                    layerTemplates.reserve(variant->layers.size());
                    for (const auto& layer : variant->layers) {
                        layerTemplates.push_back(layer.tilePath);
                    }
                    tiles.setMap(variant->key, variant->tilePath, layerTemplates);
                }
                const auto questData = quests.tasks();
                const auto features = quests.mapFeatures();
                if (overlaysFeaturesFrom != features.get() ||
                    questPlacesFor != variant->key) {
                    overlaysFeaturesFrom = features.get();
                    overlaysForMap.extracts.clear();
                    overlaysForMap.spawns.clear();
                    overlaysForMap.transits.clear();
                    for (const auto id : mapIds.idsFor(snapshot->mapNormalized)) {
                        const auto it = features->find(id);
                        if (it == features->end()) {
                            continue;
                        }
                        for (const auto& extract : it->second.extracts) {
                            ui::MapPoint point;
                            point.world = extract.position;
                            point.label = extract.name;
                            point.top = extract.top;
                            point.bottom = extract.bottom;
                            point.icon =
                                extract.faction == data::ExtractFaction::Pmc
                                    ? ui::MarkerIcon::ExtractPmc
                                : extract.faction == data::ExtractFaction::Scav
                                    ? ui::MarkerIcon::ExtractScav
                                    : ui::MarkerIcon::ExtractShared;
                            overlaysForMap.extracts.push_back(std::move(point));
                        }
                        for (const auto* spawn : data::notableSpawns(it->second)) {
                            ui::MapPoint point;
                            point.world = spawn->position;
                            point.label = spawn->boss ? ui::tr(ui::Str::BossSpawn) : ui::tr(ui::Str::PmcSpawn);
                            point.icon = spawn->boss ? ui::MarkerIcon::SpawnBoss
                                                     : ui::MarkerIcon::SpawnPmc;
                            overlaysForMap.spawns.push_back(std::move(point));
                        }
                        for (const auto& transit : it->second.transits) {
                            ui::MapPoint point;
                            point.world = transit.position;
                            const auto target = mapIds.drawnAs(transit.targetMapId);
                            point.label =
                                transit.description.empty()
                                    ? ui::trf(ui::Str::TransitTo,
                                              target ? ui::mapDisplayName(*target).c_str()
                                                     : "?")
                                    : transit.description;
                            point.icon = ui::MarkerIcon::Transit;
                            overlaysForMap.transits.push_back(std::move(point));
                        }
                    }
                    forceRedraw = true;
                }
                const std::uint64_t revision = progress.revision();
                if (questPlacesFor != variant->key ||
                    questPlacesFrom != questData.get() ||
                    questPlacesRevision != revision) {
                    questPlacesFor = variant->key;
                    questPlacesFrom = questData.get();
                    questPlacesRevision = revision;

                    const auto effective =
                        data::resolveProgress(*progress.snapshot(), *questData);
                    activeQuestCount = static_cast<int>(effective.active.size());
                    // With no known progress, filtering would show nothing and
                    // look broken. Show everything and say why instead.
                    questPlacesFiltered = activeQuestCount > 0;
                    questPlaces = ui::mergeNearbyQuestPlaces(buildQuestPlaces(
                        *questData, mapIds, snapshot->mapNormalized,
                        questPlacesFiltered ? &effective : nullptr));

                    // The list covers every quest with a place on this map,
                    // whatever its state, so it can be corrected from here.
                    questEntries.clear();
                    const auto ids = mapIds.idsFor(snapshot->mapNormalized);
                    const auto recorded = progress.snapshot();
                    for (const auto& task : questData->tasks) {
                        int here = 0;
                        for (const auto& objective : task.objectives) {
                            for (const auto& zone : objective.zones) {
                                here += std::find(ids.begin(), ids.end(), zone.mapId) !=
                                        ids.end();
                            }
                            for (const auto& location : objective.questItemLocations) {
                                here += std::find(ids.begin(), ids.end(),
                                                  location.mapId) != ids.end();
                            }
                        }
                        if (here == 0) {
                            continue;
                        }
                        ui::QuestListEntry entry;
                        entry.id = task.id;
                        entry.name = task.displayName();
                        entry.objectivesHere = here;
                        entry.state = effective.stateOf(task.id);
                        entry.inferred = effective.inferredCompleted.count(task.id) != 0;
                        const auto* record = recorded->find(task.id);
                        entry.manual = record != nullptr && record->manual;
                        questEntries.push_back(std::move(entry));
                    }
                    forceRedraw = true;
                }

                const auto result = ui::drawMapView(
                    mapState, *variant, *snapshot,
                    static_cast<std::int64_t>(nowTicks), interactive,
                    [&tiles](int layer, int zoom, int x, int y) -> ui::TileImage {
                        const auto tile = tiles.acquire({layer, zoom, x, y});
                        return {reinterpret_cast<ImTextureID>(tile.texture),
                                tile.loading};
                    },
                    [&] {
                        overlaysForMap.quests = questPlaces;
                        overlaysForMap.icons = [&icons](ui::MarkerIcon icon) -> ImTextureID {
                            const char* name = "";
                            switch (icon) {
                                case ui::MarkerIcon::QuestObjective: name = "quest_objective"; break;
                                case ui::MarkerIcon::ExtractPmc: name = "extract_pmc"; break;
                                case ui::MarkerIcon::ExtractScav: name = "extract_scav"; break;
                                case ui::MarkerIcon::ExtractShared: name = "extract_shared"; break;
                                case ui::MarkerIcon::Transit: name = "extract_transit"; break;
                                case ui::MarkerIcon::SpawnPmc: name = "spawn_pmc"; break;
                                case ui::MarkerIcon::SpawnBoss: name = "spawn_boss"; break;
                            }
                            return reinterpret_cast<ImTextureID>(icons.get(name));
                        };
                        overlaysForMap.glyphs = [&icons](data::ObjectiveKind kind) -> ImTextureID {
                            return reinterpret_cast<ImTextureID>(
                                icons.get(ui::questGlyphName(kind)));
                        };
                        return std::cref(overlaysForMap);
                    }(),
                    [&abstractMaps, variant](const geo::Vec2& imageMin,
                                             const geo::Vec2& imageMax, int w,
                                             int h) -> ui::AbstractMapImage {
                        ui::AbstractMapImage out;
                        if (variant->svgPath.empty()) {
                            return out;  // This map has no drawing.
                        }
                        const auto size = abstractMaps.documentSize();
                        const auto wanted =
                            map::imageRectToSvg(*variant, size, imageMin, imageMax);
                        const auto view = abstractMaps.acquire(
                            wanted ? *wanted : map::SvgRect{}, w, h);

                        out.available = true;
                        // Waiting for the document counts as rendering, unless
                        // it is known not to be coming.
                        out.rendering =
                            view.rendering || (!view.haveDocument && !view.failed);
                        if (view.texture != nullptr) {
                            if (const auto rect = map::svgRectToImage(*variant, size,
                                                                     view.region)) {
                                out.texture =
                                    reinterpret_cast<ImTextureID>(view.texture);
                                out.imageMin = rect->first;
                                out.imageMax = rect->second;
                            }
                        }
                        return out;
                    });
                mapLoading = result.piecesLoading > 0;
                if (result.wantsRedraw) {
                    forceRedraw = true;
                }
            }
        }

        if (mapVisible && interactive && !questEntries.empty()) {
            ui::QuestListActions actions;
            actions.setState = [&progress](const std::string& id, data::QuestState state) {
                progress.setManual(id, state, static_cast<std::int64_t>(std::time(nullptr)));
                saveProgress(progress);
            };
            actions.clearCorrection = [&progress](const std::string& id) {
                progress.clearManual(id);
                saveProgress(progress);
            };
            if (ui::drawQuestList(questEntries, questPlacesFiltered, activeQuestCount,
                                  actions)) {
                forceRedraw = true;
            }
        }

        if (hudVisible) {
            ui::drawHud(*snapshot, static_cast<std::int64_t>(nowTicks), hudOptions);
        }
        drawStatus(nowTicks);

        imgui.endFrame();
        overlay.present(true);
    }

    watcher.stop();
    quests.stop();
    icons.stop();
    abstractMaps.stop();
    tiles.stop();
    unregisterHotkeys();
    return 0;
}
