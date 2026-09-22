#include "platform/win32/SettingsWindow.h"

#include <dxgi.h>

#include <algorithm>

namespace tarkov::win32 {
namespace {

constexpr wchar_t kWindowClass[] = L"TarkovHelpSettings";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kMenuSettings = 1;
constexpr UINT kMenuQuit = 2;

/// Frames drawn after each event that can change what the window shows.
constexpr int kFramesPerEvent = 3;

/// Messages after which the window may look different. Anything else --
/// timers, notifications, the tray -- leaves the picture alone.
bool changesPicture(UINT message) {
    return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
           (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
           message == WM_MOUSELEAVE || message == WM_NCMOUSELEAVE ||
           message == WM_ACTIVATE || message == WM_SETFOCUS ||
           message == WM_KILLFOCUS || message == WM_SIZE || message == WM_PAINT ||
           message == WM_SHOWWINDOW;
}

bool isModifier(WPARAM vk) {
    switch (vk) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN:
            return true;
        default:
            return false;
    }
}

}  // namespace

SettingsWindow::~SettingsWindow() {
    if (trayAdded_) {
        Shell_NotifyIconW(NIM_DELETE, &tray_);
    }
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
    }
}

LRESULT CALLBACK SettingsWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam,
                                            LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr && self->hwnd_ == hwnd) {
        return self->handle(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT SettingsWindow::handle(UINT message, WPARAM wParam, LPARAM lParam) {
    if (changesPicture(message)) {
        framesWanted_ = kFramesPerEvent;
    }
    // Key capture comes before ImGui: the next non-modifier key is the new
    // binding, with whatever modifiers are held at that moment.
    if (capturing_ && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)) {
        if (!isModifier(wParam)) {
            if (wParam == VK_ESCAPE) {
                captured_ = input::Hotkey{};
            } else {
                input::Hotkey key;
                key.virtualKey = static_cast<unsigned>(wParam);
                if (GetKeyState(VK_CONTROL) < 0) key.modifiers |= input::kControl;
                if (GetKeyState(VK_MENU) < 0) key.modifiers |= input::kAlt;
                if (GetKeyState(VK_SHIFT) < 0) key.modifiers |= input::kShift;
                if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) {
                    key.modifiers |= input::kWin;
                }
                captured_ = key;
            }
            capturing_ = false;
        }
        return 0;  // Swallowed: Alt must not open the system menu mid-capture.
    }

    if (message == kTrayMessage) {
        switch (LOWORD(lParam)) {
            case WM_LBUTTONDBLCLK:
            case NIN_SELECT:
                openRequested_ = true;
                break;
            case WM_RBUTTONUP:
            case WM_CONTEXTMENU:
                showTrayMenu();
                break;
        }
        return 0;
    }
    if (taskbarCreated_ != 0 && message == taskbarCreated_) {
        // Explorer restarted and forgot every tray icon; put ours back.
        trayAdded_ = false;
        addTrayIcon();
        return 0;
    }

    if (ui_.handleMessage(hwnd_, message, wParam, lParam)) {
        return 0;
    }

    switch (message) {
        case WM_CLOSE:
            // Closing hides; the overlay keeps running in the tray.
            hide();
            closedByUser_ = true;
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == kMenuSettings) openRequested_ = true;
            if (LOWORD(wParam) == kMenuQuit) quitRequested_ = true;
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_KEYMENU) {
                return 0;  // Alt alone must not freeze rendering in a modal menu loop.
            }
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

bool SettingsWindow::createSwapChain() {
    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device_->QueryInterface(__uuidof(IDXGIDevice), dxgiDevice.putVoid()))) {
        return false;
    }
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.put()))) {
        return false;
    }
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory2), factory.putVoid()))) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(width_);
    desc.Height = static_cast<UINT>(height_);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(factory->CreateSwapChainForHwnd(device_, hwnd_, &desc, nullptr, nullptr,
                                               swapChain_.put()))) {
        return false;
    }
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), backBuffer.putVoid()))) {
        return false;
    }
    return SUCCEEDED(
        device_->CreateRenderTargetView(backBuffer.get(), nullptr, renderTarget_.put()));
}

bool SettingsWindow::create(HINSTANCE instance, ID3D11Device* device,
                            ID3D11DeviceContext* context, const std::wstring& title,
                            const std::wstring& tooltip) {
    device_ = device;
    context_ = context;
    tooltip_ = tooltip;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsWindow::windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    // Fixed size: the layout is designed for it, and a resizable window would
    // need the swap chain resized with it for no benefit here.
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT frame{0, 0, width_, height_};
    AdjustWindowRect(&frame, style, FALSE);
    const int frameWidth = frame.right - frame.left;
    const int frameHeight = frame.bottom - frame.top;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - frameWidth) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - frameHeight) / 2;

    hwnd_ = CreateWindowExW(0, kWindowClass, title.c_str(), style, x, y, frameWidth,
                            frameHeight, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr || !createSwapChain() ||
        !ui_.initialise(hwnd_, device_, context_)) {
        return false;
    }

    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    addTrayIcon();
    return true;
}

void SettingsWindow::addTrayIcon() {
    if (trayAdded_) {
        return;
    }
    tray_ = NOTIFYICONDATAW{};
    tray_.cbSize = sizeof(tray_);
    tray_.hWnd = hwnd_;
    tray_.uID = 1;
    tray_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray_.uCallbackMessage = kTrayMessage;
    tray_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(tray_.szTip, tooltip_.c_str(), _TRUNCATE);
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &tray_) != FALSE;
    if (trayAdded_) {
        tray_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &tray_);
    }
}

void SettingsWindow::showTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    AppendMenuW(menu, MF_STRING, kMenuSettings, trayMenuSettings_.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuQuit, trayMenuQuit_.c_str());
    POINT cursor{};
    GetCursorPos(&cursor);
    // Required for the menu to close when the user clicks elsewhere.
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, cursor.x, cursor.y, 0, hwnd_,
                   nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void SettingsWindow::show() {
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    visible_ = true;
    framesWanted_ = kFramesPerEvent;
}

bool SettingsWindow::wantsFrame() const {
    return visible_ && framesWanted_ > 0 && IsIconic(hwnd_) == FALSE;
}

void SettingsWindow::requestFrame() {
    framesWanted_ = (std::max)(framesWanted_, 1);
}

void SettingsWindow::hide() {
    ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
    capturing_ = false;
}

void SettingsWindow::render(const std::function<void()>& draw) {
    if (!visible_ || !renderTarget_) {
        return;
    }
    if (framesWanted_ > 0) {
        --framesWanted_;
    }
    const float clear[4] = {0.07f, 0.08f, 0.08f, 1.0f};
    ID3D11RenderTargetView* targets[] = {renderTarget_.get()};
    context_->OMSetRenderTargets(1, targets, nullptr);
    context_->ClearRenderTargetView(renderTarget_.get(), clear);

    ui_.beginFrame(width_, height_);
    draw();
    ui_.endFrame();
    swapChain_->Present(1, 0);
}

void SettingsWindow::startKeyCapture() {
    capturing_ = true;
    captured_.reset();
}

void SettingsWindow::stopKeyCapture() { capturing_ = false; }

std::optional<input::Hotkey> SettingsWindow::takeCapturedKey() {
    auto key = captured_;
    captured_.reset();
    return key;
}

bool SettingsWindow::takeOpenRequest() {
    return std::exchange(openRequested_, false);
}

bool SettingsWindow::takeQuitRequest() {
    return std::exchange(quitRequested_, false);
}

bool SettingsWindow::takeClosedByUser() {
    return std::exchange(closedByUser_, false);
}

void SettingsWindow::notify(const std::wstring& title, const std::wstring& text) {
    if (!trayAdded_) {
        return;
    }
    NOTIFYICONDATAW balloon = tray_;
    balloon.uFlags = NIF_INFO;
    wcsncpy_s(balloon.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(balloon.szInfo, text.c_str(), _TRUNCATE);
    balloon.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &balloon);
}

}  // namespace tarkov::win32
