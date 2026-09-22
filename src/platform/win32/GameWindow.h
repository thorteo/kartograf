#pragma once

#include <windows.h>

#include <optional>

namespace tarkov::win32 {

/// Where the mouse pointer is within `bounds`, relative to its top-left, when
/// the player can actually point with it; nullopt otherwise.
///
/// In a raid the game locks the cursor for mouselook: it clips it to a single
/// point at the centre of the screen. In menus and the inventory it releases
/// it. That clip rectangle is the signal -- the cursor's "showing" flag is not:
/// the game reports it showing while mouselook has it pinned. Verified live on
/// 2026-09-21 by logging GetClipCursor and GetCursorInfo through a raid, the
/// death screen and the menus.
///
/// Reads global cursor state only; nothing here touches the game.
std::optional<POINT> usablePointer(const RECT& bounds);

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

    /// True when the shell reports an exclusive-mode Direct3D application in
    /// the foreground, where nothing we draw will be composited on top.
    ///
    /// SHQueryUserNotificationState's QUNS_RUNNING_D3D_FULL_SCREEN, which the
    /// SDK documents as "full-screen (exclusive-mode) D3D app"; a borderless
    /// window is QUNS_BUSY instead. The window-style heuristic this replaced
    /// could not tell the two apart and warned in borderless, live, on
    /// 2026-09-21. Shell state only; the game is not touched.
    static bool exclusiveFullscreenRunning();

private:
    HWND cached_ = nullptr;
};

}  // namespace tarkov::win32
