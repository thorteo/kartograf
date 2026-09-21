#include "platform/win32/OverlayWindow.h"

#include <dxgi.h>

#include <cstdio>

#include "platform/win32/ImGuiLayer.h"

namespace tarkov::win32 {
namespace {

constexpr wchar_t kWindowClass[] = L"TarkovHelpOverlay";

// Not in every SDK's winuser.h, and we must not fail to build because of it.
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#ifndef WDA_MONITOR
#define WDA_MONITOR 0x00000001
#endif
#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif

}  // namespace

OverlayWindow::~OverlayWindow() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool OverlayWindow::fail(const char* what, HRESULT hr) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s failed (hr=0x%08lX)", what,
                  static_cast<unsigned long>(hr));
    lastError_ = buffer;
    return false;
}

LRESULT CALLBACK OverlayWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam,
                                           LPARAM lParam) {
    auto* self = reinterpret_cast<OverlayWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    // Let the UI see input first, but only when the overlay is actually
    // interactive. With click-through on, no input reaches us and forwarding
    // would leave ImGui believing the cursor is somewhere it is not.
    if (self != nullptr && self->interactive_ &&
        ImGuiLayer::handleMessage(hwnd, message, wParam, lParam)) {
        return 0;
    }

    switch (message) {
        case WM_NCCREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            break;
        }
        case WM_CLOSE:
        case WM_DESTROY:
            if (self != nullptr) {
                self->shouldClose_ = true;
            }
            return 0;
        // The overlay must never take focus away from the game, even if
        // something contrives to click it while click-through is off.
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool OverlayWindow::createWindow(HINSTANCE instance, int width, int height) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &OverlayWindow::windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (RegisterClassExW(&wc) == 0) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            return fail("RegisterClassExW", HRESULT_FROM_WIN32(error));
        }
    }

    // WS_EX_NOACTIVATE   never steal focus from the game
    // WS_EX_TOOLWINDOW   stay out of the taskbar and Alt-Tab
    // WS_EX_TRANSPARENT  click-through, toggled later
    // WS_EX_LAYERED      required for a composited transparent top-level window
    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
                          WS_EX_TOOLWINDOW | WS_EX_TOPMOST;

    hwnd_ = CreateWindowExW(exStyle, kWindowClass, L"tarkov-help", WS_POPUP, 0, 0,
                            width, height, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) {
        return fail("CreateWindowExW", HRESULT_FROM_WIN32(GetLastError()));
    }

    width_ = width;
    height_ = height;
    return true;
}

bool OverlayWindow::createDevice() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
    // Only if the debug layer is actually installed; retried without it below.
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1,
                                        D3D_FEATURE_LEVEL_11_0,
                                        D3D_FEATURE_LEVEL_10_1};

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                                   device_.put(), nullptr, context_.put());
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG) != 0) {
        flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                               levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                               device_.put(), nullptr, context_.put());
    }
    if (FAILED(hr)) {
        return fail("D3D11CreateDevice", hr);
    }
    return true;
}

bool OverlayWindow::createSwapChain(int width, int height) {
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = device_->QueryInterface(__uuidof(IDXGIDevice), dxgiDevice.putVoid());
    if (FAILED(hr)) {
        return fail("QueryInterface(IDXGIDevice)", hr);
    }

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.put());
    if (FAILED(hr)) {
        return fail("IDXGIDevice::GetAdapter", hr);
    }

    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(__uuidof(IDXGIFactory2), factory.putVoid());
    if (FAILED(hr)) {
        return fail("IDXGIAdapter::GetParent(IDXGIFactory2)", hr);
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    // The whole point: the composited surface carries real per-pixel alpha.
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = factory->CreateSwapChainForComposition(device_.get(), &desc, nullptr,
                                                swapChain_.put());
    if (FAILED(hr)) {
        return fail("CreateSwapChainForComposition", hr);
    }
    return true;
}

bool OverlayWindow::createComposition() {
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = device_->QueryInterface(__uuidof(IDXGIDevice), dxgiDevice.putVoid());
    if (FAILED(hr)) {
        return fail("QueryInterface(IDXGIDevice)", hr);
    }

    hr = DCompositionCreateDevice(dxgiDevice.get(), __uuidof(IDCompositionDevice),
                                  compositionDevice_.putVoid());
    if (FAILED(hr)) {
        return fail("DCompositionCreateDevice", hr);
    }

    hr = compositionDevice_->CreateTargetForHwnd(hwnd_, TRUE,
                                                 compositionTarget_.put());
    if (FAILED(hr)) {
        return fail("CreateTargetForHwnd", hr);
    }

    hr = compositionDevice_->CreateVisual(compositionVisual_.put());
    if (FAILED(hr)) {
        return fail("CreateVisual", hr);
    }

    hr = compositionVisual_->SetContent(swapChain_.get());
    if (FAILED(hr)) {
        return fail("IDCompositionVisual::SetContent", hr);
    }

    hr = compositionTarget_->SetRoot(compositionVisual_.get());
    if (FAILED(hr)) {
        return fail("IDCompositionTarget::SetRoot", hr);
    }

    hr = compositionDevice_->Commit();
    if (FAILED(hr)) {
        return fail("IDCompositionDevice::Commit", hr);
    }
    return true;
}

bool OverlayWindow::createRenderTarget() {
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                       backBuffer.putVoid());
    if (FAILED(hr)) {
        return fail("IDXGISwapChain1::GetBuffer", hr);
    }

    hr = device_->CreateRenderTargetView(backBuffer.get(), nullptr,
                                         renderTarget_.put());
    if (FAILED(hr)) {
        return fail("CreateRenderTargetView", hr);
    }
    return true;
}

bool OverlayWindow::create(HINSTANCE instance, int width, int height) {
    return createWindow(instance, width, height) && createDevice() &&
           createSwapChain(width, height) && createComposition() &&
           createRenderTarget();
}

void OverlayWindow::setClickThrough(bool enabled) {
    if (hwnd_ == nullptr) {
        return;
    }
    LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (enabled) {
        style |= WS_EX_TRANSPARENT;
    } else {
        style &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    }
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, style);
    clickThrough_ = enabled;
}

OverlayWindow::CaptureExclusion OverlayWindow::setExcludedFromCapture(bool enabled) {
    if (hwnd_ == nullptr) {
        return CaptureExclusion::kUnavailable;
    }
    if (!enabled) {
        SetWindowDisplayAffinity(hwnd_, WDA_NONE);
        return CaptureExclusion::kUnavailable;
    }
    // Windows 10 2004 and later hide the window from capture entirely.
    if (SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE)) {
        return CaptureExclusion::kHidden;
    }
    // Older builds can only blank it, which still keeps it out of a stream.
    if (SetWindowDisplayAffinity(hwnd_, WDA_MONITOR)) {
        return CaptureExclusion::kBlanked;
    }
    return CaptureExclusion::kUnavailable;
}

void OverlayWindow::setVisible(bool visible) {
    if (hwnd_ == nullptr) {
        return;
    }
    // SW_SHOWNOACTIVATE, never SW_SHOW: showing must not pull focus off the game.
    ShowWindow(hwnd_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void OverlayWindow::setBounds(int x, int y, int width, int height) {
    if (hwnd_ == nullptr) {
        return;
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE | SWP_NOREDRAW);

    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        renderTarget_.reset();
        if (swapChain_) {
            swapChain_->ResizeBuffers(0, static_cast<UINT>(width),
                                      static_cast<UINT>(height),
                                      DXGI_FORMAT_UNKNOWN, 0);
            createRenderTarget();
        }
    }
}

bool OverlayWindow::pumpMessages() {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            shouldClose_ = true;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return !shouldClose_;
}

void OverlayWindow::beginFrame(float r, float g, float b, float a) {
    if (!renderTarget_ || !context_) {
        return;
    }
    // Premultiplied alpha: the colour channels are already scaled by alpha, so
    // a caller passing (1, 0, 0, 0.5) for half-transparent red would be wrong;
    // it wants (0.5, 0, 0, 0.5).
    const float clear[4] = {r, g, b, a};
    ID3D11RenderTargetView* targets[] = {renderTarget_.get()};
    context_->OMSetRenderTargets(1, targets, nullptr);
    context_->ClearRenderTargetView(renderTarget_.get(), clear);
}

void OverlayWindow::present(bool vsync) {
    if (swapChain_) {
        swapChain_->Present(vsync ? 1 : 0, 0);
    }
}

void OverlayWindow::renderFrame(float r, float g, float b, float a) {
    beginFrame(r, g, b, a);
    present(true);
}

}  // namespace tarkov::win32
