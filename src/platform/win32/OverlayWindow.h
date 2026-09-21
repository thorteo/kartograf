#pragma once

#include <windows.h>

#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>

#include <string>

#include "platform/win32/ComPtr.h"

namespace tarkov::win32 {

/// A transparent, always-on-top overlay window composited by DirectComposition.
///
/// This is our own top-level window. Nothing here touches the game process:
/// no hooking, no injection, no memory access. See
/// docs/adr/0002-no-game-process-interaction.md and
/// docs/adr/0003-overlay-window.md.
///
/// Requires the game to run borderless or windowed -- Windows will not
/// composite anything over an exclusive-fullscreen swap chain.
class OverlayWindow {
public:
    OverlayWindow() = default;
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    /// Creates the window and the D3D11 + DirectComposition pipeline.
    /// Returns false with a reason in lastError().
    bool create(HINSTANCE instance, int width, int height);

    /// Lets clicks fall through to whatever is underneath. Cleared while the
    /// user is interacting with the map, restored afterwards.
    void setClickThrough(bool enabled);
    bool clickThrough() const { return clickThrough_; }

    /// Keeps the overlay out of screen recordings and streams.
    ///
    /// Uses WDA_EXCLUDEFROMCAPTURE where available (Windows 10 2004+) and falls
    /// back to WDA_MONITOR, which blanks rather than hides. Returns what was
    /// actually applied so the settings UI can be honest about it.
    enum class CaptureExclusion { kUnavailable, kBlanked, kHidden };
    CaptureExclusion setExcludedFromCapture(bool enabled);

    void setVisible(bool visible);
    void setBounds(int x, int y, int width, int height);

    /// Drains the message queue. Returns false when the window should close.
    bool pumpMessages();

    /// Clears the back buffer to a premultiplied-alpha colour and binds it.
    /// Draw between this and present().
    void beginFrame(float r, float g, float b, float a);

    /// Presents the composed frame. `vsync` off is useful when the overlay is
    /// idle and we are pacing ourselves.
    void present(bool vsync = true);

    /// Clears and presents in one call, with nothing drawn in between.
    void renderFrame(float r, float g, float b, float a);

    /// While true, window messages are forwarded to the UI layer. Turned off
    /// with click-through, when the overlay receives no input at all.
    void setInteractive(bool interactive) { interactive_ = interactive; }
    bool interactive() const { return interactive_; }

    ID3D11Device* device() const { return device_.get(); }
    ID3D11DeviceContext* context() const { return context_.get(); }
    int width() const { return width_; }
    int height() const { return height_; }

    HWND handle() const { return hwnd_; }
    const std::string& lastError() const { return lastError_; }

private:
    bool createWindow(HINSTANCE instance, int width, int height);
    bool createDevice();
    bool createSwapChain(int width, int height);
    bool createComposition();
    bool createRenderTarget();
    bool fail(const char* what, HRESULT hr);

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam,
                                       LPARAM lParam);

    HWND hwnd_ = nullptr;
    bool clickThrough_ = true;
    bool interactive_ = false;
    bool shouldClose_ = false;
    int width_ = 0;
    int height_ = 0;
    std::string lastError_;

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID3D11RenderTargetView> renderTarget_;
    ComPtr<IDCompositionDevice> compositionDevice_;
    ComPtr<IDCompositionTarget> compositionTarget_;
    ComPtr<IDCompositionVisual> compositionVisual_;
};

}  // namespace tarkov::win32
