#include "platform/win32/ImGuiLayer.h"

#include <cfloat>

#include <imgui.h>

#include <shlobj.h>

#include <string>

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include "platform/win32/Utf8.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

namespace tarkov::win32 {
namespace {

/// Loads a system font with Cyrillic coverage.
///
/// ImGui's built-in font is Latin-only: quest names from the Russian locale
/// rendered as rows of question marks until this existed. Segoe UI ships with
/// every supported Windows version, so nothing is bundled; Arial and Tahoma
/// are fallbacks. ImGui 1.92 loads glyphs on demand, so no glyph ranges are
/// needed.
bool loadSystemFont(ImGuiIO& io, float size) {
    PWSTR raw = nullptr;
    std::wstring fonts;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Fonts, 0, nullptr, &raw))) {
        fonts = raw;
        CoTaskMemFree(raw);
    }
    if (fonts.empty()) {
        return false;
    }
    for (const wchar_t* file : {L"segoeui.ttf", L"arial.ttf", L"tahoma.ttf"}) {
        const std::wstring wide = fonts + L"\\" + file;
        if (GetFileAttributesW(wide.c_str()) == INVALID_FILE_ATTRIBUTES) {
            continue;
        }
        // ImGui opens files by UTF-8 name and converts on Windows.
        if (io.Fonts->AddFontFromFileTTF(narrow(wide).c_str(), size) != nullptr) {
            return true;
        }
    }
    return false;
}

}  // namespace

namespace {

/// Makes a context current for a scope and restores the previous one.
class ContextScope {
public:
    explicit ContextScope(ImGuiContext* context) : previous_(ImGui::GetCurrentContext()) {
        ImGui::SetCurrentContext(context);
    }
    ~ContextScope() { ImGui::SetCurrentContext(previous_); }
    ContextScope(const ContextScope&) = delete;
    ContextScope& operator=(const ContextScope&) = delete;

private:
    ImGuiContext* previous_;
};

}  // namespace

ImGuiLayer::~ImGuiLayer() {
    if (!initialised_) {
        return;
    }
    ImGui::SetCurrentContext(context_);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(context_);
    context_ = nullptr;
    initialised_ = false;
}

bool ImGuiLayer::initialise(HWND hwnd, ID3D11Device* device,
                            ID3D11DeviceContext* context) {
    IMGUI_CHECKVERSION();
    ImGuiContext* previous = ImGui::GetCurrentContext();
    context_ = ImGui::CreateContext();
    if (context_ == nullptr) {
        return false;
    }
    ImGui::SetCurrentContext(context_);

    ImGuiIO& io = ImGui::GetIO();
    // No imgui.ini: the overlay owns its own layout, and writing a stray file
    // next to the executable would be rude.
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // The game owns the cursor. We never draw our own.
    io.MouseDrawCursor = false;

    applyOverlayStyle();
    loadSystemFont(io, 17.0f);

    const auto abandon = [&] {
        ImGui::DestroyContext(context_);
        context_ = nullptr;
        ImGui::SetCurrentContext(previous);
        return false;
    };
    if (!ImGui_ImplWin32_Init(hwnd)) {
        return abandon();
    }
    if (!ImGui_ImplDX11_Init(device, context)) {
        ImGui_ImplWin32_Shutdown();
        return abandon();
    }

    initialised_ = true;
    ImGui::SetCurrentContext(previous);
    return true;
}

void ImGuiLayer::beginFrame(int width, int height) {
    if (!initialised_) {
        return;
    }
    // Left current until endFrame: everything drawn in between belongs to
    // this window.
    ImGui::SetCurrentContext(context_);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();

    // The backend reads the size from the window, but the overlay is resized to
    // follow the game and the two can disagree for a frame.
    ImGui::GetIO().DisplaySize =
        ImVec2(static_cast<float>(width), static_cast<float>(height));

    if (passive_) {
        // Queued after the backend's own update, so this is what the frame sees.
        // An invalid position, not a stale one, when there is no pointer:
        // otherwise a tooltip would stay up after the game took the cursor back.
        ImGui::GetIO().AddMousePosEvent(
            pointer_ ? static_cast<float>(pointer_->x) : -FLT_MAX,
            pointer_ ? static_cast<float>(pointer_->y) : -FLT_MAX);
        passive_ = false;
    }

    ImGui::NewFrame();
}

void ImGuiLayer::setPassivePointer(std::optional<POINT> at) {
    passive_ = true;
    pointer_ = at;
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
    if (!initialised_) {
        return false;
    }
    const ContextScope scope(context_);
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
