#include "overlay.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef DrawText
#undef DrawTextW
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

Overlay::Overlay() {}

Overlay::~Overlay() {
    Destroy();
}

bool Overlay::RegisterWindowClass() {
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = m_instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)CreateSolidBrush(RGB(0, 0, 0));
    wc.lpszClassName = "FF_ESP_Overlay";
    return RegisterClassExA(&wc) != 0;
}

bool Overlay::Create(HWND targetWindow) {
    m_targetWnd = targetWindow;
    m_instance = GetModuleHandleA(nullptr);

    if (!RegisterWindowClass()) return false;

    RECT rc;
    GetClientRect(m_targetWnd, &rc);
    m_width = rc.right;
    m_height = rc.bottom;

    POINT topLeft = { 0, 0 };
    ClientToScreen(m_targetWnd, &topLeft);

    m_overlayWnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        "FF_ESP_Overlay",
        "FF ESP Overlay",
        WS_POPUP,
        topLeft.x, topLeft.y, m_width, m_height,
        nullptr, nullptr, m_instance, this
    );

    if (!m_overlayWnd) return false;

    // Store this pointer for WndProc
    SetWindowLongPtrA(m_overlayWnd, GWLP_USERDATA, (LONG_PTR)this);

    // Make click-through
    SetLayeredWindowAttributes(m_overlayWnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    // Enable DWM blur-behind for per-pixel alpha
    DWM_BLURBEHIND bb = {};
    bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
    bb.hRgnBlur = CreateRectRgn(0, 0, -1, -1);
    bb.fEnable = true;
    DwmEnableBlurBehindWindow(m_overlayWnd, &bb);

    ShowWindow(m_overlayWnd, SW_SHOW);
    SetLayeredWindowAttributes(m_overlayWnd, RGB(0, 0, 0), 255, LWA_ALPHA);

    m_running = true;
    return true;
}

void Overlay::Destroy() {
    m_running = false;
    if (m_overlayWnd) {
        DestroyWindow(m_overlayWnd);
        m_overlayWnd = nullptr;
    }
}

void Overlay::UpdateWindowPosition() {
    if (!m_targetWnd || !m_overlayWnd || !IsWindow(m_targetWnd)) return;

    if (!IsWindowVisible(m_targetWnd)) {
        ShowWindow(m_overlayWnd, SW_HIDE);
        return;
    }
    ShowWindow(m_overlayWnd, SW_SHOW);

    RECT rc;
    GetClientRect(m_targetWnd, &rc);
    POINT topLeft = { 0, 0 };
    ClientToScreen(m_targetWnd, &topLeft);

    m_width = (rc.right > 1) ? rc.right : 1;
    m_height = (rc.bottom > 1) ? rc.bottom : 1;

    SetWindowPos(m_overlayWnd, HWND_TOPMOST,
        topLeft.x, topLeft.y, m_width, m_height,
        SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

void Overlay::Run() {
    MSG msg;
    while (m_running && GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        UpdateWindowPosition();
    }
}

LRESULT CALLBACK Overlay::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTTRANSPARENT; // Make entire window click-through
    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}
