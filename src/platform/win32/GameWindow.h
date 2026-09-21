#pragma once

#include <windows.h>

namespace tarkov::win32 {

struct GameWindowState {
    bool found = false;
    bool foreground = false;  ///< The game currently has focus.
    RECT bounds{};            ///< Client area in screen coordinates.
};

/// Locates and follows the Escape from Tarkov window.
///
/// Read-only: FindWindow and GetClientRect only. Nothing here opens the process,
/// reads its memory, or injects anything. See
/// docs/adr/0002-no-game-process-interaction.md.
class GameWindowTracker {
public:
    /// Re-scans and returns the current state. Cheap enough to call each frame,
    /// though the caller should throttle to a few times a second.
    GameWindowState poll();

    /// True when the game appears to be in exclusive fullscreen, where nothing
    /// we draw will be composited on top.
    ///
    /// Heuristic: the window covers its whole monitor and has no border styles.
    /// Borderless fullscreen looks similar, so this is advisory -- use it to
    /// explain a blank overlay, not to refuse to run.
    static bool looksLikeExclusiveFullscreen(HWND hwnd);

private:
    HWND cached_ = nullptr;
};

}  // namespace tarkov::win32
