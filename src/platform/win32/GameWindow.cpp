#include "platform/win32/GameWindow.h"

#include <shellapi.h>

#include <cwctype>
#include <string>

namespace tarkov::win32 {
namespace {

// Unity games share this class name; the caption is what distinguishes EFT.
// "EscapeFromTarkov" exactly, checked with FindWindow on 2026-09-22.
constexpr wchar_t kGameClass[] = L"UnityWndClass";
constexpr wchar_t kGameTitle[] = L"EscapeFromTarkov";

bool isAlive(HWND hwnd) {
    return hwnd != nullptr && IsWindow(hwnd) && IsWindowVisible(hwnd);
}

/// A visible Unity window whose caption mentions Tarkov, in case the caption
/// varies by build or locale. The class alone is not enough: any other Unity
/// game running would do, and the overlay would follow that instead.
BOOL CALLBACK findTarkovLike(HWND hwnd, LPARAM found) {
    wchar_t className[64] = {};
    if (GetClassNameW(hwnd, className, 64) == 0 ||
        std::wstring_view(className) != kGameClass || !IsWindowVisible(hwnd)) {
        return TRUE;
    }
    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, 256);
    std::wstring lower(title);
    for (auto& c : lower) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    if (lower.find(L"tarkov") == std::wstring::npos) {
        return TRUE;
    }
    *reinterpret_cast<HWND*>(found) = hwnd;
    return FALSE;
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
            EnumWindows(&findTarkovLike, reinterpret_cast<LPARAM>(&cached_));
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

bool GameWindowTracker::exclusiveFullscreenRunning() {
    QUERY_USER_NOTIFICATION_STATE state{};
    return SUCCEEDED(SHQueryUserNotificationState(&state)) &&
           state == QUNS_RUNNING_D3D_FULL_SCREEN;
}

}  // namespace tarkov::win32
