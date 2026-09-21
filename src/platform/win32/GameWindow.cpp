#include "platform/win32/GameWindow.h"

namespace tarkov::win32 {
namespace {

// Unity games share this class name; the caption is what distinguishes EFT.
constexpr wchar_t kGameClass[] = L"UnityWndClass";
constexpr wchar_t kGameTitle[] = L"EscapeFromTarkov";

bool isAlive(HWND hwnd) {
    return hwnd != nullptr && IsWindow(hwnd) && IsWindowVisible(hwnd);
}

}  // namespace

std::optional<POINT> usablePointer(const RECT& bounds) {
    RECT clip{};
    if (!GetClipCursor(&clip)) {
        return std::nullopt;
    }
    if (clip.right - clip.left <= 1 && clip.bottom - clip.top <= 1) {
        return std::nullopt;  // Pinned for mouselook.
    }
    CURSORINFO cursor{};
    cursor.cbSize = sizeof(cursor);
    if (!GetCursorInfo(&cursor) || (cursor.flags & CURSOR_SHOWING) == 0) {
        return std::nullopt;
    }
    if (!PtInRect(&bounds, cursor.ptScreenPos)) {
        return std::nullopt;
    }
    return POINT{cursor.ptScreenPos.x - bounds.left, cursor.ptScreenPos.y - bounds.top};
}

GameWindowState GameWindowTracker::poll() {
    if (!isAlive(cached_)) {
        cached_ = FindWindowW(kGameClass, kGameTitle);
        if (cached_ == nullptr) {
            // The caption occasionally differs by build or locale; fall back to
            // the class alone rather than losing the window entirely.
            cached_ = FindWindowW(kGameClass, nullptr);
        }
    }

    GameWindowState state;
    if (!isAlive(cached_)) {
        cached_ = nullptr;
        return state;
    }

    state.found = true;
    state.foreground = (GetForegroundWindow() == cached_);

    RECT client{};
    if (GetClientRect(cached_, &client)) {
        POINT topLeft{client.left, client.top};
        POINT bottomRight{client.right, client.bottom};
        ClientToScreen(cached_, &topLeft);
        ClientToScreen(cached_, &bottomRight);
        state.bounds = RECT{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    }
    return state;
}

bool GameWindowTracker::looksLikeExclusiveFullscreen(HWND hwnd) {
    if (hwnd == nullptr) {
        return false;
    }
    RECT window{};
    if (!GetWindowRect(hwnd, &window)) {
        return false;
    }

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return false;
    }

    const bool coversMonitor = window.left <= info.rcMonitor.left &&
                               window.top <= info.rcMonitor.top &&
                               window.right >= info.rcMonitor.right &&
                               window.bottom >= info.rcMonitor.bottom;

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const bool borderless = (style & (WS_BORDER | WS_THICKFRAME | WS_CAPTION)) == 0;

    return coversMonitor && borderless;
}

}  // namespace tarkov::win32
