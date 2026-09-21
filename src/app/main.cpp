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
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>

#include "core/data/EmbeddedMaps.h"
#include "core/game/RaidState.h"
#include "core/game/ScreenshotName.h"
#include "core/geo/MapConfig.h"
#include "core/input/Hotkey.h"
#include "platform/win32/GameDataWatcher.h"
#include "platform/win32/GameWindow.h"
#include "platform/win32/ImGuiLayer.h"
#include "platform/win32/OverlayWindow.h"
#include "platform/win32/TileStore.h"
#include "ui/Hud.h"
#include "ui/MapView.h"

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
        g_status.add("could not parse '" + text + "', using " +
                     formatHotkey(fallback));
        return fallback;
    }
    if (isReservedCombination(*parsed)) {
        g_status.add("'" + text + "' is reserved, using " + formatHotkey(fallback));
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

void registerOrReport(int id, const tarkov::input::Hotkey& hotkey,
                      const char* purpose) {
    if (RegisterHotKey(nullptr, id, hotkey.modifiers | MOD_NOREPEAT,
                       hotkey.virtualKey)) {
        g_status.add(std::string(purpose) + ": " +
                     tarkov::input::formatHotkey(hotkey));
        return;
    }
    const DWORD error = GetLastError();
    if (error == ERROR_HOTKEY_ALREADY_REGISTERED) {
        g_status.add(tarkov::input::formatHotkey(hotkey) +
                     " is already claimed by another application");
    } else {
        g_status.add("could not bind " + tarkov::input::formatHotkey(hotkey));
    }
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

}  // namespace

int main(int argc, char** argv) {
    using namespace tarkov;

    const input::Hotkey mapKey =
        resolveHotkey(argumentValue(argc, argv, "--toggle-key"),
                      input::defaultToggleHotkey());
    const input::Hotkey interact =
        resolveHotkey(argumentValue(argc, argv, "--interact-key"),
                      input::Hotkey{input::kAlt, 'N'});
    const input::Hotkey hudKey =
        resolveHotkey(argumentValue(argc, argv, "--hud-key"),
                      input::Hotkey{input::kAlt, 'H'});
    const bool debug = hasFlag(argc, argv, "--debug");
    // Development and first-run aids: show the overlay with no game running,
    // and allow it into screen captures so it can be photographed.
    const bool standalone = hasFlag(argc, argv, "--no-game");
    // Development aids: open a named map with no raid running, and inject a
    // position by pasting a real screenshot filename. Together these let the
    // marker be checked against the map without playing.
    const std::string forcedMap = argumentValue(argc, argv, "--map");
    const std::string fakeShot = argumentValue(argc, argv, "--fake-shot");
    const bool allowCapture = hasFlag(argc, argv, "--allow-capture");
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
    if (!imgui.initialise(overlay.handle(), overlay.device(), overlay.context())) {
        MessageBoxA(nullptr, "Could not initialise the interface layer.",
                    "tarkov-help", MB_ICONERROR | MB_OK);
        return 1;
    }

    switch (overlay.setExcludedFromCapture(!allowCapture)) {
        case win32::OverlayWindow::CaptureExclusion::kHidden:
            g_status.add("hidden from screen capture");
            break;
        case win32::OverlayWindow::CaptureExclusion::kBlanked:
            g_status.add("blanked in screen capture (Windows 10 2004+ hides it)");
            break;
        case win32::OverlayWindow::CaptureExclusion::kUnavailable:
            g_status.add(allowCapture ? "visible to screen capture"
                                      : "could not exclude from screen capture");
            break;
    }

    registerOrReport(kMapHotkeyId, mapKey, "map");
    registerOrReport(kInteractHotkeyId, interact, "interact");
    registerOrReport(kHudHotkeyId, hudKey, "hud");

    const std::vector<geo::MapDefinition> maps = geo::parseMapsJson(data::kMapsJson);
    if (maps.empty()) {
        g_status.add("embedded map data failed to parse -- the map view is disabled");
    }

    win32::TileStore tiles;
    tiles.start(overlay.device());

    game::RaidState raidState;
    if (!forcedMap.empty()) {
        game::RaidEvent forced;
        forced.type = game::RaidEventType::MapLoading;
        forced.mapId = forcedMap;
        raidState.apply(forced, static_cast<std::int64_t>(GetTickCount64()));
        g_status.add("forced map: " + forcedMap);
    }
    if (!fakeShot.empty()) {
        if (const auto shot = game::parseScreenshotName(fakeShot)) {
            raidState.applyPosition(*shot,
                                    static_cast<std::int64_t>(GetTickCount64()));
            g_status.add("injected position from screenshot name");
        } else {
            g_status.add("could not parse the screenshot name given to --fake-shot");
        }
    }

    win32::GameDataWatcher watcher;
    if (!watcher.start(raidState, [](const std::string& line) { g_status.add(line); })) {
        g_status.add("running without game data");
    }

    win32::GameWindowTracker tracker;
    bool overlayEnabled = true;
    bool interactive = false;
    bool warnedAboutFullscreen = false;

    ui::HudOptions hudOptions;
    hudOptions.showDebug = debug;

    ui::MapViewState mapState;
    // Asking for a map on the command line means you want to see it.
    bool mapVisible = !forcedMap.empty();
    bool hudVisible = true;
    std::string tiledMap;

    std::shared_ptr<const game::RaidSnapshot> lastSnapshot;
    unsigned long long lastDisplaySecond = 0;
    // Set whenever something outside the snapshot changes what is on screen.
    bool forceRedraw = true;
    // Tiles arrive asynchronously, so a frame with missing tiles has to be
    // followed by more frames until they land.
    bool pendingTiles = false;

    const ULONGLONG startedAt = GetTickCount64();
    if (standalone) {
        g_status.add("standalone preview -- not attached to the game");
    }

    while (overlay.pumpMessages()) {
        MSG message;
        while (PeekMessageW(&message, nullptr, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
            if (message.message != WM_HOTKEY) {
                continue;
            }
            if (message.wParam == kMapHotkeyId) {
                mapVisible = !mapVisible;
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
                g_status.add(interactive ? "interactive -- clicks go to the overlay"
                                         : "click-through");
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
            game.foreground = true;
            game.bounds = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN),
                               GetSystemMetrics(SM_CYSCREEN)};
        }
        const bool shouldShow = overlayEnabled && game.found && game.foreground;
        overlay.setVisible(shouldShow);

        if (!shouldShow) {
            // Nothing to composite. Idle cheaply rather than spinning.
            Sleep(60);
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
            g_status.add("if nothing shows, switch the game to borderless");
        }

        const auto snapshot = raidState.snapshot();
        const auto nowTicks = GetTickCount64();

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
        const bool needsFrame = interactive || stateChanged || secondTicked ||
                                forceRedraw || uploaded > 0 || pendingTiles;

        if (!needsFrame) {
            // Wake early if a message arrives, so input stays responsive.
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
            continue;
        }

        lastSnapshot = snapshot;
        lastDisplaySecond = displaySecond;
        forceRedraw = false;

        // Fully transparent: only what we draw is visible over the game.
        overlay.beginFrame(0.0f, 0.0f, 0.0f, 0.0f);
        imgui.beginFrame(overlay.width(), overlay.height());

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
                    ImGui::Text("Waiting for a raid to start.");
                    ImGui::TextDisabled("The map opens by itself once the game "
                                        "says which one you are on.");
                }
                ImGui::End();
            } else {
                if (tiledMap != variant->key) {
                    tiledMap = variant->key;
                    std::vector<std::string> layerTemplates;
                    layerTemplates.reserve(variant->layers.size());
                    for (const auto& layer : variant->layers) {
                        layerTemplates.push_back(layer.tilePath);
                    }
                    tiles.setMap(variant->key, variant->tilePath, layerTemplates);
                }
                const auto result = ui::drawMapView(
                    mapState, *variant, *snapshot,
                    static_cast<std::int64_t>(nowTicks), interactive,
                    [&tiles](int layer, int zoom, int x, int y) -> ImTextureID {
                        return reinterpret_cast<ImTextureID>(
                            tiles.acquire({layer, zoom, x, y}));
                    });
                // Keep drawing while tiles are still arriving, then stop.
                pendingTiles = result.tilesRequested > 0;
                if (result.wantsRedraw) {
                    forceRedraw = true;
                }
            }
        } else {
            pendingTiles = false;
        }

        if (hudVisible) {
            ui::drawHud(*snapshot, static_cast<std::int64_t>(nowTicks), hudOptions);
        }
        drawStatus(nowTicks);

        imgui.endFrame();
        overlay.present(true);
    }

    watcher.stop();
    tiles.stop();
    UnregisterHotKey(nullptr, kMapHotkeyId);
    UnregisterHotKey(nullptr, kHudHotkeyId);
    UnregisterHotKey(nullptr, kInteractHotkeyId);
    return 0;
}
