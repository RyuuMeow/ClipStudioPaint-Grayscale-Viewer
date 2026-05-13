#include "magnification/MagnifierOverlay.h"
#include "util/Logger.h"
#include <dwmapi.h>
#include <mmsystem.h>
#include <shobjidl.h>

#pragma comment(lib, "winmm.lib")

namespace csp::magnification
{

    MagnifierOverlay* MagnifierOverlay::Instance = nullptr;

    namespace
    {
        constexpr wchar_t kHostClassName[] = L"CSP_GrayscaleOverlay";
        constexpr int kDelayFrames = 3;
        constexpr UINT_PTR kTimerId = 1;

        // BT.601 grayscale color matrix
        constexpr MAGCOLOREFFECT kGrayscaleEffect = {{
            { 0.299f, 0.299f, 0.299f, 0.0f, 0.0f },
            { 0.587f, 0.587f, 0.587f, 0.0f, 0.0f },
            { 0.114f, 0.114f, 0.114f, 0.0f, 0.0f },
            { 0.0f,   0.0f,   0.0f,   1.0f, 0.0f },
            { 0.0f,   0.0f,   0.0f,   0.0f, 1.0f }
        }};
    }

    // ─── Lifecycle ───────────────────────────────────────────────────────

    MagnifierOverlay::~MagnifierOverlay()
    {
        Shutdown();
    }

    bool MagnifierOverlay::Initialize()
    {
        if (Initialized)
        {
            return true;
        }

        if (!MagInitialize())
        {
            LOG_ERROR(L"MagInitialize failed (error %u)", GetLastError());
            return false;
        }

        Instance = this;

        if (!CreateHostWindow())
        {
            MagUninitialize();
            Instance = nullptr;
            return false;
        }

        if (!CreateMagnifierControl())
        {
            DestroyWindow(HostWnd);
            HostWnd = nullptr;
            MagUninitialize();
            Instance = nullptr;
            return false;
        }

        Initialized = true;
        LOG_INFO(L"MagnifierOverlay initialized");
        return true;
    }

    void MagnifierOverlay::Shutdown()
    {
        if (!Initialized)
        {
            return;
        }

        Hide();

        if (MagWnd)
        { DestroyWindow(MagWnd);  MagWnd = nullptr; }
        if (HostWnd)
        { DestroyWindow(HostWnd); HostWnd = nullptr; }

        MagUninitialize();
        Instance = nullptr;
        Initialized = false;
        LOG_INFO(L"MagnifierOverlay shut down");
    }

    // ─── Host Window ─────────────────────────────────────────────────────

    bool MagnifierOverlay::CreateHostWindow()
    {
        WNDCLASSEXW wc = {};
        wc.cbSize         = sizeof(wc);
        wc.lpfnWndProc    = HostWndProc;
        wc.hInstance      = GetModuleHandleW(nullptr);
        wc.lpszClassName  = kHostClassName;
        wc.hCursor        = LoadCursorW(nullptr, IDC_ARROW);

        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            LOG_ERROR(L"RegisterClassEx failed (error %u)", GetLastError());
            return false;
        }

        // WS_EX_LAYERED + WS_EX_TRANSPARENT: required combo for click-through
        // WS_EX_TOPMOST: always on top of CSP
        // WS_EX_TOOLWINDOW: no taskbar entry
        // WS_EX_NOACTIVATE: never steals focus
        HostWnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED |
            WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
            kHostClassName,
            L"",
            WS_POPUP,
            0, 0, 1, 1,
            nullptr, nullptr,
            GetModuleHandleW(nullptr),
            nullptr
        );

        if (!HostWnd)
        {
            LOG_ERROR(L"CreateWindowEx host failed (error %u)", GetLastError());
            return false;
        }

        // Fully opaque but layered — required for WS_EX_TRANSPARENT click-through
        SetLayeredWindowAttributes(HostWnd, 0, 255, LWA_ALPHA);

        // Explicitly tell Windows this is NOT a fullscreen window to prevent
        // the OS from automatically turning on "Focus Assist" / "Do Not Disturb"
        ITaskbarList2* pTaskbarList = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pTaskbarList))))
        {
            pTaskbarList->HrInit();
            pTaskbarList->MarkFullscreenWindow(HostWnd, FALSE);
            pTaskbarList->Release();
        }

        return true;
    }

    // ─── Magnifier Control ───────────────────────────────────────────────

    bool MagnifierOverlay::CreateMagnifierControl()
    {
        MagWnd = CreateWindowExW(
            0,
            WC_MAGNIFIER,
            L"MagnifierControl",
            WS_CHILD | WS_VISIBLE,
            0, 0, 1, 1,
            HostWnd, nullptr,
            GetModuleHandleW(nullptr),
            nullptr
        );

        if (!MagWnd)
        {
            LOG_ERROR(L"CreateWindowEx magnifier failed (error %u)", GetLastError());
            return false;
        }

        // 1:1 magnification (no zoom)
        MAGTRANSFORM matrix = {};
        matrix.v[0][0] = 1.0f;
        matrix.v[1][1] = 1.0f;
        matrix.v[2][2] = 1.0f;
        if (!MagSetWindowTransform(MagWnd, &matrix))
        {
            LOG_WARNING(L"MagSetWindowTransform failed (error %u)", GetLastError());
        }

        // Grayscale color effect
        if (!MagSetColorEffect(MagWnd, const_cast<PMAGCOLOREFFECT>(&kGrayscaleEffect)))
        {
            LOG_WARNING(L"MagSetColorEffect failed (error %u)", GetLastError());
        }

        // Exclude host window from capture to prevent feedback loop
        HWND excludeList[] = { HostWnd };
        if (!MagSetWindowFilterList(MagWnd, MW_FILTERMODE_EXCLUDE, 1, excludeList))
        {
            LOG_WARNING(L"MagSetWindowFilterList failed (error %u)", GetLastError());
        }

        return true;
    }

    // ─── Show / Hide / Attach ────────────────────────────────────────────

    void MagnifierOverlay::Attach(HWND targetWindow)
    {
        TargetWnd = targetWindow;
    }

    void MagnifierOverlay::Detach()
    {
        Hide();
        TargetWnd = nullptr;
    }

    void MagnifierOverlay::Show()
    {
        if (!Initialized || !TargetWnd)
        {
            return;
        }
        if (Visible)
        {
            return;
        }

        // Temporarily make the window almost invisible to hide the old frame
        SetLayeredWindowAttributes(HostWnd, 0, 1, LWA_ALPHA);
        ShowFrames = 0; // Reset frame counter for fade-in

        Visible = true;
        EnableHighResolutionTimer();
        UpdateOverlay();
        
        // Force immediate synchronous redraw to prevent old frames from flashing
        UpdateWindow(HostWnd);
        UpdateWindow(MagWnd);

        StartRefreshLoop();
        LOG_INFO(L"Grayscale overlay shown");
    }

    void MagnifierOverlay::Hide()
    {
        if (!Visible)
        {
            return;
        }

        StopRefreshLoop();
        DisableHighResolutionTimer();
        ShowWindow(HostWnd, SW_HIDE);
        Visible = false;

        LOG_INFO(L"Grayscale overlay hidden");
    }

    void MagnifierOverlay::SetRefreshRate(int fps)
    {
        if (fps < 1)
        {
            fps = 1;
        }
        if (fps > 240)
        {
            fps = 240;
        }
        UINT interval = static_cast<UINT>(1000 / fps);
        if (interval < 1)
        {
            interval = 1;
        }
        TimerIntervalMs = interval;
        LOG_INFO(L"Refresh rate set to %d fps (%u ms)", fps, interval);
    }

    void MagnifierOverlay::EnableHighResolutionTimer()
    {
        if (HighResolutionTimerEnabled)
        {
            return;
        }

        if (timeBeginPeriod(1) == TIMERR_NOERROR)
        {
            HighResolutionTimerEnabled = true;
        }
        else
        {
            LOG_WARNING(L"timeBeginPeriod(1) failed; timer precision may be limited");
        }
    }

    void MagnifierOverlay::DisableHighResolutionTimer()
    {
        if (!HighResolutionTimerEnabled)
        {
            return;
        }

        timeEndPeriod(1);
        HighResolutionTimerEnabled = false;
    }

    void MagnifierOverlay::StartRefreshLoop()
    {
        if (!HostWnd)
        {
            return;
        }

        SetTimer(HostWnd, kTimerId, TimerIntervalMs, nullptr);
    }

    void MagnifierOverlay::StopRefreshLoop()
    {
        if (!HostWnd)
        {
            return;
        }

        KillTimer(HostWnd, kTimerId);
    }

    // ─── Frame Update ────────────────────────────────────────────────────

    void MagnifierOverlay::UpdateOverlay()
    {
        if (!TargetWnd || !IsWindow(TargetWnd))
        {
            Hide();
            return;
        }

        bool isWindowVisible = IsWindowVisible(HostWnd);

        // Skip if target is minimized
        if (IsIconic(TargetWnd))
        {
            if (isWindowVisible)
            {
                ShowWindow(HostWnd, SW_HIDE);
            }
            return;
        }
        else if (Visible)
        {
            if (!isWindowVisible)
            {
                ShowWindow(HostWnd, SW_SHOWNOACTIVATE);
            }
        }

        // Get target window's screen rectangle
        // Use DwmGetWindowAttribute to get visual bounds (excluding invisible drop shadow/borders)
        RECT rc;
        if (FAILED(DwmGetWindowAttribute(TargetWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc))))
        {
            GetWindowRect(TargetWnd, &rc);
        }

        int w = rc.right  - rc.left;
        int h = rc.bottom - rc.top;

        if (w <= 0 || h <= 0)
        {
            return;
        }

        // Only reposition windows if CSP actually moved or resized
        bool rectChanged = (rc.left != LastRect.left || rc.top != LastRect.top ||
                            rc.right != LastRect.right || rc.bottom != LastRect.bottom);

        if (rectChanged)
        {
            LastRect = rc;

            // Move host window to cover target exactly
            SetWindowPos(HostWnd, HWND_TOPMOST,
                         rc.left, rc.top, w, h,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);

            // Resize magnifier control to fill host
            SetWindowPos(MagWnd, nullptr,
                         0, 0, w, h,
                         SWP_NOZORDER | SWP_NOACTIVATE);

        }

        // Refresh the captured source every frame. The magnifier control does
        // not reliably pick up high-frequency drawing changes from invalidation alone.
        RECT sourceRect = { rc.left, rc.top, rc.right, rc.bottom };
        MagSetWindowSource(MagWnd, sourceRect);

        // Always invalidate to refresh content (user may be drawing).
        // FALSE = don't erase background, reduces flicker
        InvalidateRect(MagWnd, nullptr, FALSE);

        // Fade-in mechanism: Wait 2 frames before making the window fully visible
        // This gives the Magnifier control enough time to capture the new screen state
        if (ShowFrames < kDelayFrames)
        {
            ShowFrames++;
            if (ShowFrames == kDelayFrames)
            {
                SetLayeredWindowAttributes(HostWnd, 0, 255, LWA_ALPHA);
            }
        }
    }

    // ─── Window Procedure ────────────────────────────────────────────────

    LRESULT CALLBACK MagnifierOverlay::HostWndProc(
        HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_TIMER:
            if (wParam == kTimerId && Instance)
            {
                Instance->UpdateOverlay();
            }
            return 0;

        case WM_NCHITTEST:
            // Pass all mouse interaction through to the window below
            return HTTRANSPARENT;

        case WM_DESTROY:
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

}
