#pragma once

#include <windows.h>

#include <d3d11.h>

namespace tarkov::win32 {

/// Binds Dear ImGui to the overlay's existing D3D11 device and window.
///
/// Immediate mode suits this application: the overlay is static most of the
/// time, so we draw only when something changed and pay nothing in between.
class ImGuiLayer {
public:
    ImGuiLayer() = default;
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    bool initialise(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);

    void beginFrame(int width, int height);
    void endFrame();

    /// Forwards a window message to ImGui. Returns true when ImGui consumed it.
    ///
    /// Only call this while the overlay is interactive: with click-through on,
    /// the overlay receives no input at all and ImGui must not believe
    /// otherwise.
    static bool handleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    bool ready() const { return initialised_; }

private:
    bool initialised_ = false;
};

/// Applies the overlay's visual style: dark, translucent, legible over a game.
void applyOverlayStyle();

}  // namespace tarkov::win32
