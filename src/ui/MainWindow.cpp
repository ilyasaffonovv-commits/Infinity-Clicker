#include "ui/Ui.h"

#include "core/Clock.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/Str.h"

#include <infclick/Version.h>
#include "../../resources/resource.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace infclick::ui {

namespace {

struct Ui {
    App* app = nullptr;
    HWND hwnd = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* swap = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    UINT resizeW = 0, resizeH = 0;
    float dpi = 1.0f;
    bool dpiChanged = false;
    bool quit = false;
    bool occluded = false;
    int framesPending = 3;
    Tray tray;
    UINT taskbarCreated = 0;
    HICON iconBig = nullptr, iconSmall = nullptr, iconActive = nullptr;
};
Ui g;

void createRtv()
{
    ID3D11Texture2D* back = nullptr;
    g.swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        g.dev->CreateRenderTargetView(back, nullptr, &g.rtv);
        back->Release();
    }
}

void releaseRtv()
{
    if (g.rtv) {
        g.rtv->Release();
        g.rtv = nullptr;
    }
}

bool createDevice(HWND h)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = h;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                               &sd, &g.swap, &g.dev, &got, &g.ctx);
    if (hr == DXGI_ERROR_UNSUPPORTED) // no GPU driver: software rasterizer
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                           &g.swap, &g.dev, &got, &g.ctx);
    if (FAILED(hr)) return false;
    createRtv();
    return true;
}

void destroyDevice()
{
    releaseRtv();
    if (g.swap) g.swap->Release();
    if (g.ctx) g.ctx->Release();
    if (g.dev) g.dev->Release();
    g.swap = nullptr;
    g.ctx = nullptr;
    g.dev = nullptr;
}

void applyDpi()
{
    g.dpi = float(GetDpiForWindow(g.hwnd)) / 96.0f;
    applyTheme(g.dpi);
}

void showWindow()
{
    ShowWindow(g.hwnd, IsIconic(g.hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g.hwnd);
    g.framesPending = 3;
}

void updateTray()
{
    App& app = *g.app;
    const EngineSnapshot& s = app.snap();
    std::string tip = "Infinity Clicker - ";
    switch (s.state) {
    case EngineState::Running: tip += strFormat(tr("ACTIVE %.1f CPS"), s.cps1s); break;
    case EngineState::Paused: tip += tr("PAUSED"); break;
    case EngineState::Armed: tip += std::string(tr("READY")) + " (" + app.profile().trigger.name() + ")"; break;
    default: tip += tr("STOPPED"); break;
    }
    tip += std::string("\n") + tr("Profile: ") + app.profile().name;
    g.tray.update(s.state, tip);
}

void saveWindowPlacement()
{
    WINDOWPLACEMENT wp{sizeof wp};
    if (!GetWindowPlacement(g.hwnd, &wp)) return;
    AppSettings& st = g.app->settings();
    const RECT& r = wp.rcNormalPosition;
    st.windowX = r.left;
    st.windowY = r.top;
    st.windowW = int(float(r.right - r.left) / g.dpi);
    st.windowH = int(float(r.bottom - r.top) / g.dpi);
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp)) return TRUE;
    if (msg == g.taskbarCreated && g.taskbarCreated) {
        g.tray.readd();
        return 0;
    }
    switch (msg) {
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) {
            if (g.app && g.app->settings().minimizeToTray) ShowWindow(h, SW_HIDE);
            return 0;
        }
        g.resizeW = LOWORD(lp);
        g.resizeH = HIWORD(lp);
        g.framesPending = 2;
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        const float s = g.hwnd ? float(GetDpiForWindow(h)) / 96.0f : 1.0f;
        mmi->ptMinTrackSize = POINT{LONG(620 * s), LONG(480 * s)};
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        g.dpiChanged = true;
        return 0;
    }
    case WM_CLOSE:
        // wp == 1: explicit exit (tray menu / restart). Otherwise honour "close to tray".
        if (wp != 1 && g.app && g.app->settings().closeToTray) {
            ShowWindow(h, SW_HIDE);
            return 0;
        }
        saveWindowPlacement();
        DestroyWindow(h);
        return 0;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION:
        if (wp && g.app) {
            saveWindowPlacement();
            g.app->shutdown(); // release held input before the session ends
        }
        return 0;
    case WM_DESTROY:
        g.quit = true;
        PostQuitMessage(0);
        return 0;
    case WM_APP_ENGINE:
        if (wp == 1 && g.app) g.app->setStatus(tr("EMERGENCY STOP - output halted, held input released, disarmed"));
        g.framesPending = 2;
        return 0;
    case WM_APP_CAPTURED:
        if (g.app) g.app->applyCaptured();
        g.framesPending = 2;
        return 0;
    case WM_APP_GATE: g.framesPending = 2; return 0;
    case WM_APP_INVOKE: {
        auto* fn = reinterpret_cast<const std::function<void()>*>(lp);
        if (fn) (*fn)();
        g.framesPending = 2;
        return 0;
    }
    case WM_APP_SHOW: showWindow(); return 0;
    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONDBLCLK:
        case NIN_SELECT:
        case NIN_KEYSELECT: showWindow(); break;
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            if (g.app) g.tray.showMenu(*g.app, h);
            break;
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0; // no Alt menu beep
        break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void render()
{
    if (g.resizeW && g.resizeH) {
        releaseRtv();
        g.swap->ResizeBuffers(0, g.resizeW, g.resizeH, DXGI_FORMAT_UNKNOWN, 0);
        g.resizeW = g.resizeH = 0;
        createRtv();
    }
    if (g.dpiChanged) {
        g.dpiChanged = false;
        applyDpi();
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    drawRoot(*g.app);
    ImGui::Render();
    const float clear[4] = {11 / 255.f, 14 / 255.f, 23 / 255.f, 1};
    g.ctx->OMSetRenderTargets(1, &g.rtv, nullptr);
    g.ctx->ClearRenderTargetView(g.rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    const HRESULT hr = g.swap->Present(1, 0);
    g.occluded = hr == DXGI_STATUS_OCCLUDED;
}

} // namespace

int run(App& app, bool startHidden, std::function<void(HWND)> onReady)
{
    g.app = &app;
    const HINSTANCE hi = GetModuleHandleW(nullptr);
    g.iconBig = static_cast<HICON>(LoadImageW(hi, MAKEINTRESOURCEW(IDI_INFCLICK), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                              GetSystemMetrics(SM_CYICON), 0));
    g.iconSmall = static_cast<HICON>(LoadImageW(hi, MAKEINTRESOURCEW(IDI_INFCLICK), IMAGE_ICON,
                                                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    g.iconActive = static_cast<HICON>(LoadImageW(hi, MAKEINTRESOURCEW(IDI_INFCLICK_ACTIVE), IMAGE_ICON,
                                                 GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));

    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hi;
    wc.hIcon = g.iconBig;
    wc.hIconSm = g.iconSmall;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"InfinityClickerMainWindow";
    RegisterClassExW(&wc);

    const AppSettings& st = app.settings();
    const float sysScale = float(GetDpiForSystem()) / 96.0f;
    int w = int(float(st.windowW) * sysScale), h = int(float(st.windowH) * sysScale);
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (st.windowX != INT32_MIN) {
        RECT r{st.windowX, st.windowY, st.windowX + w, st.windowY + h};
        if (MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) {
            x = st.windowX;
            y = st.windowY;
        }
    }
    const std::wstring title = L"Infinity Clicker";
    g.hwnd = CreateWindowExW(0, wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, hi,
                             nullptr);
    if (!g.hwnd) return 1;
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(g.hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
    const COLORREF caption = RGB(11, 14, 23);
    DwmSetWindowAttribute(g.hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof caption);

    if (!createDevice(g.hwnd)) {
        MessageBoxW(g.hwnd, toWide(tr("Direct3D 11 initialisation failed.")).c_str(), L"Infinity Clicker", MB_ICONERROR);
        return 1;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // no imgui.ini files - settings live in settings.json
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplWin32_Init(g.hwnd);
    ImGui_ImplDX11_Init(g.dev, g.ctx);
    loadFonts();
    applyDpi();

    app.setUiWindow(g.hwnd);
    if (app.settings().alwaysOnTop) SetWindowPos(g.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    g.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g.tray.add(g.hwnd, g.iconSmall, g.iconActive ? g.iconActive : g.iconSmall);
    if (!(startHidden || st.startMinimized)) {
        ShowWindow(g.hwnd, SW_SHOWDEFAULT);
        UpdateWindow(g.hwnd);
    }
    if (onReady) onReady(g.hwnd);

    MSG msg;
    while (!g.quit) {
        const bool visible = IsWindowVisible(g.hwnd) && !IsIconic(g.hwnd);
        const EngineState es = app.engine().state();
        const bool busy = es == EngineState::Running || es == EngineState::Paused ||
                          app.capturing() != CaptureTarget::None || app.pad().isOpen() || app.labRunActive();
        DWORD timeout = INFINITE;
        if (g.framesPending > 0) timeout = 0;
        else if (visible) timeout = busy ? 33 : 500;
        else timeout = busy ? 500 : 2000; // hidden: only tray tooltip / housekeeping
        MsgWaitForMultipleObjectsEx(0, nullptr, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g.quit = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message != WM_TIMER && msg.message != WM_NULL) g.framesPending = std::max(g.framesPending, 2);
        }
        if (g.quit) break;
        app.uiHeartbeat.store(clk::now(), std::memory_order_relaxed);
        app.tick();
        updateTray();
        if (IsWindowVisible(g.hwnd) && !IsIconic(g.hwnd)) {
            if (g.occluded && g.swap->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
                g.framesPending = 0;
                continue;
            }
            render();
            if (g.framesPending > 0) --g.framesPending;
        } else {
            g.framesPending = 0;
        }
    }

    g.tray.remove();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroyDevice();
    app.setUiWindow(nullptr);
    app.saveNow();
    return 0;
}

} // namespace infclick::ui
