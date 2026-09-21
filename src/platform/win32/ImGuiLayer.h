#pragma once

#include <windows.h>

#include <d3d11.h>

// ImGui's own type, declared at global scope: declaring it inside the namespace
// below would create a different, unrelated type.
struct ImGuiContext;

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

    /// Forwards a window message to this layer's ImGui. Returns true when ImGui
    /// consumed it.
    ///
    /// Each window has its own ImGui context -- the overlay and the settings
    /// window both draw with ImGui -- so every entry point makes its own
    /// context current first and restores the previous one after. ImGui's
    /// "current context" is a global, and a message for one window arriving
    /// while the other's context is current would otherwise be fed to the
    /// wrong UI.
    bool handleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    bool ready() const { return initialised_; }

private:
    bool initialised_ = false;
    ::ImGuiContext* context_ = nullptr;
};

/// Applies the overlay's visual style: dark, translucent, legible over a game.
void applyOverlayStyle();

}  // namespace tarkov::win32
