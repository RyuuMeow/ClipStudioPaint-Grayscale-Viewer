#include "magnification/MagnifierOverlay.h"
#include "util/Logger.h"
#include <dwmapi.h>
#include <mmsystem.h>
#include <shobjidl.h>

#pragma comment(lib, "winmm.lib")

namespace csp::magnification
{

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

    bool MagnifierOverlay::InitializeResources()
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

        LastRect = {};

        if (!CreateHostWindow())
        {
            MagUninitialize();
            return false;
        }

        if (!CreateMagnifierControl())
        {
            DestroyWindow(HostWnd);
            MagWnd = nullptr;
            HostWnd = nullptr;
            MagUninitialize();
            return false;
        }

        Initialized = true;
        ResourcesReady.store(true);
        LOG_INFO(L"MagnifierOverlay initialized");
        return true;
    }

    void MagnifierOverlay::DestroyResources()
    {
        if (!Initialized)
        {
            return;
        }

        HideOnWorker();
        ResourcesReady.store(false);

        if (MagWnd)
        { DestroyWindow(MagWnd);  MagWnd = nullptr; }
        if (HostWnd)
        { DestroyWindow(HostWnd); HostWnd = nullptr; }

        MagUninitialize();
        Initialized = false;
        TargetWnd = nullptr;
        LastRect = {};
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
            this
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
            return false;
        }

        // Exclude host window from capture to prevent feedback loop
        HWND excludeList[] = { HostWnd };
        if (!MagSetWindowFilterList(MagWnd, MW_FILTERMODE_EXCLUDE, 1, excludeList))
        {
            LOG_WARNING(L"MagSetWindowFilterList failed (error %u)", GetLastError());
            return false;
        }

        return true;
    }

    // ─── Show / Hide / Attach ────────────────────────────────────────────

    void MagnifierOverlay::ShowOnWorker()
    {
        if (!Initialized || !TargetWnd || !RequestIsCurrent())
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
        if (!Visible || !RequestIsCurrent()) { HideOnWorker(); return; }
        
        // Force immediate synchronous redraw to prevent old frames from flashing
        UpdateWindow(HostWnd);
        UpdateWindow(MagWnd);

        if (!RequestIsCurrent()) { HideOnWorker(); return; }
        StartRefreshLoop();
    }

    void MagnifierOverlay::HideOnWorker()
    {
        if (!Visible)
        {
            return;
        }

        Visible = false;
        Presented.store(false);
        StopRefreshLoop();
        DisableHighResolutionTimer();
        ShowWindow(HostWnd, SW_HIDE);
        Notify();

        LOG_INFO(L"Grayscale overlay hidden");
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
        if (!HostWnd || !Visible)
        {
            return;
        }

        if (!SetTimer(HostWnd, kTimerId, TimerIntervalMs, nullptr))
        {
            LOG_ERROR(L"Cannot start Magnification timer (error %u)", GetLastError());
            HideOnWorker();
            ReportFailure();
        }
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
        if (!Visible || !RequestIsCurrent()) return;
        if (!TargetWnd || !IsWindow(TargetWnd))
        {
            HideOnWorker();
            return;
        }

        bool isWindowVisible = IsWindowVisible(HostWnd);

        // Skip if target is minimized
        if (IsIconic(TargetWnd))
        {
            if (isWindowVisible)
            {
                ShowWindow(HostWnd, SW_HIDE);
                Presented.store(false);
                ShowFrames = 0;
                SetLayeredWindowAttributes(HostWnd, 0, 1, LWA_ALPHA);
                Notify();
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
        RECT rc{};
        if (FAILED(DwmGetWindowAttribute(TargetWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc))))
        {
            if (!GetWindowRect(TargetWnd, &rc)) { HideOnWorker(); return; }
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
        if (!MagSetWindowSource(MagWnd, sourceRect))
        {
            LOG_ERROR(L"MagSetWindowSource failed (error %u)", GetLastError());
            HideOnWorker();
            ReportFailure();
            return;
        }

        if (!RequestIsCurrent()) { HideOnWorker(); return; }

        // Always invalidate to refresh content (user may be drawing).
        // FALSE = don't erase background, reduces flicker
        InvalidateRect(MagWnd, nullptr, FALSE);

        // Wait for three source updates before making the window fully visible.
        // This gives the Magnifier control enough time to capture the new screen state
        if (ShowFrames < kDelayFrames)
        {
            ShowFrames++;
            if (ShowFrames == kDelayFrames)
            {
                SetLayeredWindowAttributes(HostWnd, 0, 255, LWA_ALPHA);
                if (!RequestIsCurrent()) { HideOnWorker(); return; }
                PresentedRevision.store(WorkerRevision);
                Presented.store(true);
                Notify();
                LOG_INFO(L"Grayscale overlay shown");
            }
        }
    }

    // ─── Window Procedure ────────────────────────────────────────────────

    LRESULT CALLBACK MagnifierOverlay::HostWndProc(
        HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* self = reinterpret_cast<MagnifierOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE)
        {
            self = static_cast<MagnifierOverlay*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        switch (msg)
        {
        case WM_TIMER:
            if (wParam == kTimerId && self)
            {
                self->UpdateOverlay();
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
