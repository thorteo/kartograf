#include "platform/win32/ImGuiLayer.h"

#include <imgui.h>

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

namespace tarkov::win32 {

ImGuiLayer::~ImGuiLayer() {
    if (!initialised_) {
        return;
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    initialised_ = false;
}

bool ImGuiLayer::initialise(HWND hwnd, ID3D11Device* device,
                            ID3D11DeviceContext* context) {
    IMGUI_CHECKVERSION();
    if (ImGui::CreateContext() == nullptr) {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    // No imgui.ini: the overlay owns its own layout, and writing a stray file
    // next to the executable would be rude.
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // The game owns the cursor. We never draw our own.
    io.MouseDrawCursor = false;

    applyOverlayStyle();

    if (!ImGui_ImplWin32_Init(hwnd)) {
        return false;
    }
    if (!ImGui_ImplDX11_Init(device, context)) {
        ImGui_ImplWin32_Shutdown();
        return false;
    }

    initialised_ = true;
    return true;
}

void ImGuiLayer::beginFrame(int width, int height) {
    if (!initialised_) {
        return;
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();

    // The backend reads the size from the window, but the overlay is resized to
    // follow the game and the two can disagree for a frame.
    ImGui::GetIO().DisplaySize =
        ImVec2(static_cast<float>(width), static_cast<float>(height));

    ImGui::NewFrame();
}

void ImGuiLayer::endFrame() {
    if (!initialised_) {
        return;
    }
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

bool ImGuiLayer::handleMessage(HWND hwnd, UINT message, WPARAM wParam,
                               LPARAM lParam) {
    return ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam) != 0;
}

void applyOverlayStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    // Softer geometry reads better as an overlay than ImGui's default sharp
    // corners, which look like a debug tool sitting on the game.
    style.WindowRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);

    ImVec4* colors = style.Colors;
    // Deep, desaturated background so bright game pixels stay readable through
    // and around it.
    colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.06f, 0.07f, 0.88f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.07f, 0.08f, 0.60f);
    colors[ImGuiCol_Border] = ImVec4(0.32f, 0.35f, 0.33f, 0.55f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.15f, 0.14f, 0.75f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.09f, 0.08f, 0.95f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.14f, 0.12f, 0.95f);
    // Tarkov's own palette leans olive; borrowing it keeps the overlay from
    // looking bolted on.
    colors[ImGuiCol_Text] = ImVec4(0.86f, 0.87f, 0.83f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.50f, 0.46f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.23f, 0.18f, 0.85f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.29f, 0.34f, 0.25f, 0.90f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.36f, 0.42f, 0.30f, 0.95f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.23f, 0.18f, 0.80f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.29f, 0.34f, 0.25f, 0.85f);
    colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.30f, 0.26f, 0.60f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.72f, 0.78f, 0.55f, 1.00f);
}

}  // namespace tarkov::win32
