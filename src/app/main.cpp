// tarkov-help -- entry point.
//
// At this milestone the overlay proves the platform foundation works end to
// end: a transparent DirectComposition surface over the game, a rebindable
// global hotkey that toggles it, and the game window tracked so the overlay
// follows it and hides when the game is not in front.
//
// The map and quest layers come next; see docs/features/roadmap.md.

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

#include "core/input/Hotkey.h"
#include "platform/win32/GameWindow.h"
#include "platform/win32/OverlayWindow.h"

namespace {

constexpr int kToggleHotkeyId = 1;

/// Reads the toggle binding from the command line, so it can be exercised
/// before a settings UI exists. The binding is a first-class user setting and
/// will move into the config file.
tarkov::input::Hotkey resolveToggleHotkey(int argc, char** argv, std::string& note) {
    using namespace tarkov::input;

    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) != "--toggle-key") {
            continue;
        }
        const auto parsed = parseHotkey(argv[i + 1]);
        if (!parsed) {
            note = std::string("could not parse '") + argv[i + 1] +
                   "', using the default";
            break;
        }
        if (isReservedCombination(*parsed)) {
            note = std::string("'") + argv[i + 1] +
                   "' is reserved by Windows or needed by the game, using the default";
            break;
        }
        return *parsed;
    }
    return defaultToggleHotkey();
}

void report(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vfprintf(stdout, format, args);
    va_end(args);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
    std::string note;
    const tarkov::input::Hotkey toggle = resolveToggleHotkey(argc, argv, note);
    if (!note.empty()) {
        report("hotkey: %s", note.c_str());
    }

    HINSTANCE instance = GetModuleHandleW(nullptr);

    tarkov::win32::OverlayWindow overlay;
    if (!overlay.create(instance, 1280, 720)) {
        report("overlay: %s", overlay.lastError().c_str());
        return 1;
    }

    if (!RegisterHotKey(nullptr, kToggleHotkeyId, toggle.modifiers | MOD_NOREPEAT,
                        toggle.virtualKey)) {
        const DWORD error = GetLastError();
        // Distinguishing these matters: the settings UI has to tell the user
        // whether to pick a different combination or whether something else is
        // wrong.
        if (error == ERROR_HOTKEY_ALREADY_REGISTERED) {
            report("hotkey: %s is already claimed by another application -- "
                   "choose a different combination",
                   tarkov::input::formatHotkey(toggle).c_str());
        } else {
            report("hotkey: could not register %s (error %lu)",
                   tarkov::input::formatHotkey(toggle).c_str(),
                   static_cast<unsigned long>(error));
        }
    } else {
        report("hotkey: %s toggles the overlay",
               tarkov::input::formatHotkey(toggle).c_str());
    }

    switch (overlay.setExcludedFromCapture(true)) {
        case tarkov::win32::OverlayWindow::CaptureExclusion::kHidden:
            report("capture: overlay hidden from screen recording");
            break;
        case tarkov::win32::OverlayWindow::CaptureExclusion::kBlanked:
            report("capture: overlay blanked in recordings (needs Windows 10 2004+ "
                   "to hide it entirely)");
            break;
        case tarkov::win32::OverlayWindow::CaptureExclusion::kUnavailable:
            report("capture: could not exclude the overlay from recording");
            break;
    }

    tarkov::win32::GameWindowTracker tracker;
    bool overlayEnabled = true;
    bool warnedAboutFullscreen = false;
    bool reportedGame = false;

    report("running. Escape from Tarkov must be in borderless or windowed mode.");

    while (overlay.pumpMessages()) {
        MSG message;
        while (PeekMessageW(&message, nullptr, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
            if (message.message == WM_HOTKEY &&
                message.wParam == kToggleHotkeyId) {
                overlayEnabled = !overlayEnabled;
                report("overlay %s", overlayEnabled ? "shown" : "hidden");
            }
        }

        const auto game = tracker.poll();

        if (game.found && !reportedGame) {
            reportedGame = true;
            report("game window found");
        }

        // Only draw while the game is in front. Compositing a fullscreen
        // transparent surface is the most expensive thing we can do, so we do
        // not do it over the desktop.
        const bool shouldShow = overlayEnabled && game.found && game.foreground;
        overlay.setVisible(shouldShow);

        if (shouldShow) {
            const int width = game.bounds.right - game.bounds.left;
            const int height = game.bounds.bottom - game.bounds.top;
            if (width > 0 && height > 0) {
                overlay.setBounds(game.bounds.left, game.bounds.top, width, height);
            }

            if (!warnedAboutFullscreen &&
                tarkov::win32::GameWindowTracker::looksLikeExclusiveFullscreen(
                    GetForegroundWindow())) {
                warnedAboutFullscreen = true;
                report("note: if nothing appears, switch the game to borderless -- "
                       "Windows cannot composite over exclusive fullscreen");
            }

            // Premultiplied alpha. A faint tint, enough to prove compositing
            // works without obscuring the game.
            overlay.renderFrame(0.0f, 0.06f, 0.10f, 0.20f);
        } else {
            Sleep(50);
        }
    }

    UnregisterHotKey(nullptr, kToggleHotkeyId);
    return 0;
}
