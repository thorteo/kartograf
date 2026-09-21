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

#include <imgui.h>

#include "core/game/RaidState.h"
#include "core/input/Hotkey.h"
#include "platform/win32/GameDataWatcher.h"
#include "platform/win32/GameWindow.h"
#include "platform/win32/ImGuiLayer.h"
#include "platform/win32/OverlayWindow.h"
#include "ui/Hud.h"

namespace {

constexpr int kToggleHotkeyId = 1;
constexpr int kInteractHotkeyId = 2;

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

    const input::Hotkey toggle =
        resolveHotkey(argumentValue(argc, argv, "--toggle-key"),
                      input::defaultToggleHotkey());
    const input::Hotkey interact =
        resolveHotkey(argumentValue(argc, argv, "--interact-key"),
                      input::Hotkey{input::kAlt, 'N'});
    const bool debug = hasFlag(argc, argv, "--debug");
    // Development and first-run aids: show the overlay with no game running,
    // and allow it into screen captures so it can be photographed.
    const bool standalone = hasFlag(argc, argv, "--no-game");
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

    registerOrReport(kToggleHotkeyId, toggle, "toggle");
    registerOrReport(kInteractHotkeyId, interact, "interact");

    game::RaidState raidState;
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

    std::shared_ptr<const game::RaidSnapshot> lastSnapshot;
    unsigned long long lastDisplaySecond = 0;
    // Set whenever something outside the snapshot changes what is on screen.
    bool forceRedraw = true;

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
            if (message.wParam == kToggleHotkeyId) {
                overlayEnabled = !overlayEnabled;
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
        const bool needsFrame =
            interactive || stateChanged || secondTicked || forceRedraw;

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

        ui::drawHud(*snapshot, static_cast<std::int64_t>(nowTicks), hudOptions);
        drawStatus(nowTicks);

        imgui.endFrame();
        overlay.present(true);
    }

    watcher.stop();
    UnregisterHotKey(nullptr, kToggleHotkeyId);
    UnregisterHotKey(nullptr, kInteractHotkeyId);
    return 0;
}
