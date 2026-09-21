#pragma once

#include <imgui.h>

namespace tarkov::test {

/// Runs Dear ImGui with no platform or renderer backend.
///
/// ImGui builds its draw lists in plain memory, so the parts of a view that are
/// actually logic -- which floor to show, whether to follow the player, how the
/// viewport responds to input -- can be exercised on Linux without a window,
/// a GPU, or Windows. Only the pixels are missing, and those are what the
/// screenshots check.
class HeadlessImGui {
public:
    explicit HeadlessImGui(float width = 1600.0f, float height = 900.0f) {
        IMGUI_CHECKVERSION();
        context_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(width, height);
        io.DeltaTime = 1.0f / 60.0f;

        // ImGui 1.92 owns font-atlas texture lifetime and asks a backend to
        // service the requests. Declaring the flag and then servicing nothing
        // is fine here: draw lists are still built, which is all these tests
        // read. Glyphs simply never reach a GPU that does not exist.
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    }

    ~HeadlessImGui() {
        if (context_ != nullptr) {
            ImGui::DestroyContext(context_);
        }
    }

    HeadlessImGui(const HeadlessImGui&) = delete;
    HeadlessImGui& operator=(const HeadlessImGui&) = delete;

    void beginFrame() { ImGui::NewFrame(); }
    void endFrame() { ImGui::Render(); }

    /// Runs one complete frame around the given body.
    template <typename Fn>
    void frame(Fn&& body) {
        beginFrame();
        body();
        endFrame();
    }

    ImGuiIO& io() { return ImGui::GetIO(); }

private:
    ImGuiContext* context_ = nullptr;
};

}  // namespace tarkov::test
