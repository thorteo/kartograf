#pragma once

#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <shellapi.h>

#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "core/input/Hotkey.h"
#include "platform/win32/ComPtr.h"
#include "platform/win32/ImGuiLayer.h"

namespace tarkov::win32 {

/// The settings window, and the tray icon that brings it back.
///
/// An ordinary window -- in the taskbar, focusable, closable -- unlike the
/// overlay, which can never take focus. Closing it only hides it: the overlay
/// keeps running, and the tray icon is how the player reopens settings or
/// quits. Before this there was no way to quit short of killing the process.
class SettingsWindow {
public:
    ~SettingsWindow();

    SettingsWindow() = default;
    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    /// Creates the window (hidden) on the overlay's D3D device, plus the tray
    /// icon. `tooltip` is the tray tooltip.
    bool create(HINSTANCE instance, ID3D11Device* device, ID3D11DeviceContext* context,
                const std::wstring& title, const std::wstring& tooltip);

    void show();
    void hide();
    bool visible() const { return visible_; }

    /// Whether a frame should be drawn now: the window is shown, not
    /// minimised, and something happened to it -- input, focus, a repaint --
    /// or requestFrame() asked. Rendering follows the overlay's rule: a window
    /// left open behind the game once drew every vsync for the whole session.
    bool wantsFrame() const;

    /// Asks for a frame, e.g. because the status it shows has changed.
    void requestFrame();

    /// Renders one frame: `draw` issues the ImGui calls.
    void render(const std::function<void()>& draw);

    /// Starts listening for the next key press to use as a hotkey. The caller
    /// must unregister its global hotkeys first, or a key already bound would
    /// be swallowed by RegisterHotKey instead of arriving here.
    void startKeyCapture();
    void stopKeyCapture();
    /// The captured key, once pressed. An invalid Hotkey means Esc: cancelled.
    std::optional<input::Hotkey> takeCapturedKey();

    /// Tray menu requests, consumed by reading them.
    bool takeOpenRequest();
    bool takeQuitRequest();
    /// True once, the first time the window was closed rather than finished
    /// with "Done", so the caller can explain where the program went.
    bool takeClosedByUser();

    /// Localised labels for the tray menu.
    void setTrayMenuLabels(std::wstring settings, std::wstring quit) {
        trayMenuSettings_ = std::move(settings);
        trayMenuQuit_ = std::move(quit);
    }

    /// Shows a one-off balloon from the tray icon.
    void notify(const std::wstring& title, const std::wstring& text);

    HWND handle() const { return hwnd_; }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam,
                                       LPARAM lParam);
    LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam);
    bool createSwapChain();
    void addTrayIcon();
    void showTrayMenu();

    HWND hwnd_ = nullptr;
    bool visible_ = false;
    int width_ = 720;
    int height_ = 780;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID3D11RenderTargetView> renderTarget_;
    ImGuiLayer ui_;

    NOTIFYICONDATAW tray_{};
    bool trayAdded_ = false;
    UINT taskbarCreated_ = 0;
    std::wstring tooltip_;
    std::wstring trayMenuSettings_ = L"Settings";
    std::wstring trayMenuQuit_ = L"Quit";

    bool capturing_ = false;
    std::optional<input::Hotkey> captured_;
    bool openRequested_ = false;
    bool quitRequested_ = false;
    bool closedByUser_ = false;
    /// Frames still to draw. Input asks for a few rather than one, so ImGui
    /// can settle hover and active states that change a frame after the event.
    int framesWanted_ = 0;
};

}  // namespace tarkov::win32
