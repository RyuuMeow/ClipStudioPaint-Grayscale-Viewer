#include "d3d/D3DOverlayRenderer.h"
#include "magnification/MagnifierOverlay.h"
#include "util/Logger.h"
#include "core/Application.h"
#include "d3d/RenderPolicy.h"
#include "window/WindowGeometry.h"
#include "tray/NotificationIcon.h"
#include <Windows.h>
#include <dwmapi.h>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <shellapi.h>
#include <vector>

namespace
{
    DWORD LongestHotkeyQueueWait = 0;
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    template<class Predicate> bool WaitUntil(Predicate predicate, DWORD timeout = 4000)
    {
        const auto end = GetTickCount64() + timeout;
        do
        {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_HOTKEY)
                    LongestHotkeyQueueWait = (std::max)(LongestHotkeyQueueWait, GetTickCount() - msg.time);
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (predicate()) return true;
            MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        } while (GetTickCount64() < end);
        return predicate();
    }
    void PumpFor(DWORD duration) { WaitUntil([] { return false; }, duration); }
    BYTE Opacity(HWND window)
    {
        BYTE alpha = 255;
        DWORD flags = 0;
        Check(GetLayeredWindowAttributes(window, nullptr, &alpha, &flags) && (flags & LWA_ALPHA),
            "overlay uses explicit opacity");
        return alpha;
    }
    HWND OwnOverlay(const wchar_t* className)
    {
        struct Query { const wchar_t* Name; HWND Result = nullptr; } query{className};
        EnumWindows([](HWND hwnd, LPARAM data) -> BOOL
        {
            DWORD process = 0;
            GetWindowThreadProcessId(hwnd, &process);
            if (process != GetCurrentProcessId()) return TRUE;
            wchar_t name[128]{};
            GetClassNameW(hwnd, name, _countof(name));
            auto& query = *reinterpret_cast<Query*>(data);
            if (wcscmp(name, query.Name) != 0) return TRUE;
            query.Result = hwnd;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&query));
        return query.Result;
    }
    LRESULT CALLBACK TargetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_TIMER && wp == 2)
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, GetWindowLongPtrW(hwnd, GWLP_USERDATA) + 1);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (msg == WM_PAINT)
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            RECT rect{};
            GetClientRect(hwnd, &rect);
            // One-channel, one-level changes exercise capture without flashing.
            const int shade = static_cast<int>(GetWindowLongPtrW(hwnd, GWLP_USERDATA) % 2);
            HBRUSH brush = CreateSolidBrush(RGB(220 + shade, 60, 100));
            FillRect(dc, &rect, brush);
            DeleteObject(brush);
            EndPaint(hwnd, &paint);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    void TestD3D(HWND target)
    {
        csp::d3d::D3DOverlayRenderer renderer;
        Check(renderer.Initialize(), "D3D worker initialization");
        Check(!renderer.HasDeviceResources(), "D3D initializes without a GPU allocation");
        const HWND overlay = OwnOverlay(L"CSP_D3DOverlay");
        Check(overlay && !IsWindowVisible(overlay) && Opacity(overlay) == 0,
            "unprepared D3D host starts transparent");
        renderer.Attach(target);
        renderer.SetRefreshRate(60);
        const auto initialStart = std::chrono::steady_clock::now();
        renderer.Show();
        Check(WaitUntil([&] { return renderer.IsVisible() || renderer.HasFailed(); }), "D3D first-frame timeout");
        Check(renderer.IsVisible() && !renderer.HasFailed(), "D3D first frame (requires unlocked GPU desktop)");
        std::cout << "D3D initial activation: " << std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - initialStart).count() << " ms\n";
        Check(IsWindowVisible(overlay) && Opacity(overlay) == 255, "prepared D3D overlay really visible");
        const auto beforeResize = renderer.PresentedFrameCount();
        SetWindowPos(target, nullptr, 110, 110, 350, 240, SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(target, nullptr, FALSE);
        RECT bounds{}, expected{};
        // This target is borderless. A pre-resize frame may still be completing;
        // wait for geometry as well as a frame, rather than racing that frame.
        GetWindowRect(target, &expected);
        Check(WaitUntil([&]
        {
            GetWindowRect(overlay, &bounds);
            return EqualRect(&bounds, &expected) && renderer.PresentedFrameCount() > beforeResize;
        }), "D3D follows target bounds and renders after resize");
        for (int i = 0; i < 20; ++i)
        {
            renderer.Hide();
            Check(WaitUntil([&] { return !IsWindowVisible(overlay); }), "D3D hides");
            const auto hiddenFrames = renderer.PresentedFrameCount();
            PumpFor(25);
            Check(!IsWindowVisible(overlay) && renderer.PresentedFrameCount() == hiddenFrames,
                "hidden D3D cannot redraw or reappear from stale work");
            renderer.Show();
            Check(WaitUntil([&] { return renderer.IsVisible() || renderer.HasFailed(); }), "D3D resumes");
            Check(!renderer.HasFailed(), "D3D resume remains healthy");
            Check(Opacity(overlay) == 255, "D3D becomes opaque after preparation");
        }
        for (int i = 0; i < 8; ++i)
        {
            renderer.Hide();
            Check(WaitUntil([&] { return !IsWindowVisible(overlay); }), "D3D hides before warm-up cancellation");
            renderer.Show();
            // Cancel at different points around acquisition and composition. A
            // fast compositor may finish between polls; that is not a failure.
            const DWORD delays[]{0, 1, 3, 5, 8, 13, 21, 34};
            if (delays[i]) PumpFor(delays[i]);
            renderer.Hide();
            Check(WaitUntil([&] { return !IsWindowVisible(overlay); }), "first-frame wait can be cancelled");
            PumpFor(50);
            Check(!IsWindowVisible(overlay) && !renderer.IsVisible(), "cancelled first frame stays hidden");
        }
        renderer.Detach();
        const auto deviceCount = renderer.DeviceCreationCount();
        const auto idleFrames = renderer.PresentedFrameCount();
        Check(WaitUntil([&] { return !renderer.HasFrameResources(); }, 6500), "D3D frees idle image buffers");
        Check(renderer.HasDeviceResources(), "D3D retains its device for fast resume");
        Check(renderer.PresentedFrameCount() == idleFrames, "D3D does not present while idle");
        Check(!IsWindowVisible(overlay), "D3D stays hidden after resource release");
        renderer.Attach(target);
        const auto resumeStart = std::chrono::steady_clock::now();
        renderer.Show();
        Check(WaitUntil([&] { return renderer.IsVisible() || renderer.HasFailed(); }), "D3D restarts after resource release");
        Check(!renderer.HasFailed(), "D3D restart remains healthy");
        Check(renderer.DeviceCreationCount() == deviceCount, "D3D resumes without recreating its device");
        std::cout << "D3D resume after idle: " << std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - resumeStart).count() << " ms\n";
        // A display change must invalidate the cache and reveal only a ready frame.
        Check(PostMessageW(overlay, WM_DISPLAYCHANGE, 0, 0) != FALSE, "post own renderer display-change event");
        Check(WaitUntil([&]
        {
            return (renderer.DeviceCreationCount() > deviceCount && renderer.IsVisible()) || renderer.HasFailed();
        }), "D3D recovers after invalidating the cached device");
        Check(!renderer.HasFailed() && Opacity(overlay) == 255, "replacement device reveals a prepared frame");
        renderer.Shutdown();
        Check(!IsWindow(overlay) && !renderer.HasDeviceResources() && !renderer.HasFrameResources(),
            "D3D shutdown releases window and all GPU resources");
        std::cout << "D3D: transparent preparation, 20 hide/show cycles, 8 warm-up cancellations, resize, idle trimming, device reuse and invalidation passed.\n";
    }
    void TestMagnification(HWND target)
    {
        csp::magnification::MagnifierOverlay renderer;
        Check(renderer.Initialize(), "Magnification initialization");
        renderer.Attach(target);
        renderer.SetRefreshRate(60);
        renderer.Show();
        Check(WaitUntil([&] { return renderer.IsVisible() || renderer.HasFailed(); }), "Magnification first-frame timeout");
        HWND overlay = OwnOverlay(L"CSP_GrayscaleOverlay");
        Check(overlay && renderer.IsVisible() && IsWindowVisible(overlay), "Magnification first show");
        Check(GetWindowThreadProcessId(overlay, nullptr) != GetCurrentThreadId(),
            "Magnification windows and capture run outside the hotkey thread");
        Check(Opacity(overlay) == 255, "Magnification reports active only after warm-up");
        for (int i = 0; i < 20; ++i)
        {
            renderer.Hide();
            // Reproduce a WM_TIMER already queued before KillTimer.
            PostMessageW(overlay, WM_TIMER, 1, 0);
            Check(WaitUntil([&] { return !IsWindowVisible(overlay); }), "Magnification hide completes");
            PumpFor(20);
            Check(!IsWindowVisible(overlay) && !renderer.IsVisible(), "stale Mag timer cannot reshow hidden overlay");
            renderer.Show();
            Check(WaitUntil([&] { return renderer.IsVisible() || renderer.HasFailed(); }), "Magnification resume timeout");
            Check(renderer.IsVisible() && IsWindowVisible(overlay) && !renderer.HasFailed(), "Mag resumes at unchanged bounds");
        }
        for (int i = 0; i < 8; ++i)
        {
            renderer.Hide();
            Check(WaitUntil([&] { return !IsWindowVisible(overlay); }), "Mag hides before warm-up cancellation");
            renderer.Show();
            Check(WaitUntil([&] { return IsWindowVisible(overlay) && Opacity(overlay) == 1; }), "Mag warm-up stays transparent");
            Check(!renderer.IsVisible(), "Mag warm-up is not reported as active");
            renderer.Hide();
            Check(WaitUntil([&] { return !IsWindowVisible(overlay); }), "Mag warm-up cancelled");
            PumpFor(70);
            Check(!IsWindowVisible(overlay) && !renderer.IsVisible(), "Mag cancelled warm-up stays hidden");
        }
        const auto releaseStart = std::chrono::steady_clock::now();
        renderer.ReleaseResources();
        Check(std::chrono::steady_clock::now() - releaseStart < std::chrono::milliseconds(100),
            "Mag resource release does not wait on capture");
        Check(WaitUntil([&] { return !renderer.HasResources() && !IsWindow(overlay); }), "Mag asynchronously releases idle resources");
        renderer.Show();
        Check(WaitUntil([&] { return renderer.IsVisible() || renderer.HasFailed(); }), "Mag resumes after resource release");
        overlay = OwnOverlay(L"CSP_GrayscaleOverlay");
        Check(renderer.IsVisible() && !renderer.HasFailed() && overlay, "Mag recreated resources remain healthy");
        renderer.Shutdown();
        Check(!IsWindow(overlay), "Magnification shutdown destroys overlay");
        Check(renderer.Initialize(), "Magnification reinitialization");
        renderer.Attach(target);
        renderer.Show();
        Check(WaitUntil([&] { return renderer.IsVisible() || renderer.HasFailed(); }), "Magnification restart timeout");
        overlay = OwnOverlay(L"CSP_GrayscaleOverlay");
        Check(renderer.IsVisible() && IsWindowVisible(overlay), "Magnification restart at unchanged bounds");
        renderer.Shutdown();
        std::cout << "Magnification: worker ownership, 20 hide/show cycles, 8 warm-up cancellations, stale timers, asynchronous release and restart passed.\n";
    }
    void TestApplicationToggles(HWND target, bool useD3D)
    {
        LongestHotkeyQueueWait = 0;
        // Exercise the real hotkey-to-application path using only this test's windows.
        wchar_t executable[MAX_PATH]{};
        GetModuleFileNameW(nullptr, executable, _countof(executable));
        csp::core::Settings settings;
        settings.TargetApps = {std::filesystem::path(executable).filename().wstring()};
        settings.ToggleHotkey = {MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F24};
        settings.UseD3D = useD3D;
        const wchar_t* activeStatus = useD3D ? L"Active - Direct3D" : L"Active - Magnification";
        const wchar_t* overlayClass = useD3D ? L"CSP_D3DOverlay" : L"CSP_GrayscaleOverlay";
        std::cout << "Application toggle backend: " << (useD3D ? "Direct3D" : "Magnification") << '\n';
        // Keep real desktop focus untouched; only this application's foreground lookup
        // sees the test-owned target. OS keyboard delivery is a separate manual check.
        HWND foreground = target;
        csp::core::Application app([&] { return foreground; });
        Check(app.Initialize(settings) && app.IsHotkeyRegistered(), "application test shortcut registered");
        HWND control = nullptr;
        while ((control = FindWindowExW(HWND_MESSAGE, control, L"CSP_GrayscaleControl", nullptr)))
        {
            DWORD process = 0;
            GetWindowThreadProcessId(control, &process);
            if (process == GetCurrentProcessId()) break;
        }
        Check(control != nullptr, "find own application control window");
        csp::tray::NotificationIcon icon;
        NOTIFYICONDATAW iconData{};
        iconData.cbSize = sizeof(iconData);
        iconData.hWnd = control;
        iconData.uID = 37;
        iconData.uFlags = NIF_ICON | NIF_TIP;
        iconData.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcscpy_s(iconData.szTip, L"CSP toggle integration test");
        Check(icon.Start(iconData) && WaitUntil([&] { return icon.IsRegistered(); }), "create own test notification icon");
        double slowestIcon = 0;
        app.SetStateCallback([&](bool, bool)
        {
            const auto start = std::chrono::steady_clock::now();
            icon.UpdateTooltip(app.StatusText());
            const auto iconMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            slowestIcon = (std::max)(slowestIcon, iconMs);
        });
        const auto start = std::chrono::steady_clock::now();
        double slowest = 0;
        for (int i = 0; i < 101; ++i)
        {
            const auto keyStart = std::chrono::steady_clock::now();
            SendMessageW(control, WM_HOTKEY, 1, 0);
            const auto ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - keyStart).count();
            slowest = (std::max)(slowest, ms);
            Check(app.IsGrayscaleEnabled() == (i % 2 == 0), "every hotkey changes requested state");
            PumpFor(1);
        }
        std::cout << "101 application toggles: " << std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count() << " ms, slowest handler: " << slowest << " ms\n";
        std::cout << "Slowest icon request so far: " << slowestIcon << " ms\n";
        Check(slowest < 100, "renderer startup/capture must not block the shortcut handler for 100 ms");
        Check(WaitUntil([&] { return app.StatusText() == activeStatus; }),
            "rapid toggles settle on the last requested renderer state");
        if (IsZoomed(target))
        {
            MONITORINFO monitor{sizeof(monitor)};
            Check(GetMonitorInfoW(MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE,
                "read maximized overlay monitor");
            RECT overlayBounds{};
            GetWindowRect(OwnOverlay(overlayClass), &overlayBounds);
            Check(EqualRect(&overlayBounds, &monitor.rcMonitor),
                "maximized overlay matches visible monitor pixels without an eight-pixel offset");
        }
        double resumeTotal = 0;
        for (int i = 0; i < 10; ++i)
        {
            // Post from a separate producer while the UI keeps pumping and the
            // renderer is active, so queue stalls cannot hide behind SendMessage.
            std::jthread producer([control]
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                PostMessageW(control, WM_HOTKEY, 1, 0);
            });
            Check(WaitUntil([&] { return !app.IsGrayscaleEnabled(); }), "queued off tap is processed during capture");
            const HWND ownOverlay = OwnOverlay(overlayClass);
            Check(WaitUntil([&] { return !IsWindowVisible(ownOverlay); }), "application hides between tap measurements");
            const auto resume = std::chrono::steady_clock::now();
            SendMessageW(control, WM_HOTKEY, 1, 0);
            Check(WaitUntil([&] { return app.StatusText() == activeStatus; }), "each tap becomes visible");
            resumeTotal += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - resume).count();
        }
        std::cout << "10 short-pause toggles, mean activation: " << resumeTotal / 10 << " ms\n";
        SendMessageW(control, WM_HOTKEY, 1, 0);
        Check(!app.IsGrayscaleEnabled(), "final hotkey turns preview off");
        const HWND overlay = OwnOverlay(overlayClass);
        Check(WaitUntil([&] { return !IsWindowVisible(overlay); }), "rapid toggles settle off");
        for (int batch = 0; batch < 100; ++batch)
        {
            for (int i = 0; i < 21; ++i)
                Check(PostMessageW(control, WM_HOTKEY, 1, 0) != FALSE, "queue own hotkey burst");
            const bool expectedOn = batch % 2 == 0;
            Check(WaitUntil([&]
            {
                return app.IsGrayscaleEnabled() == expectedOn &&
                    (expectedOn ? app.StatusText() == activeStatus : !IsWindowVisible(overlay));
            }), "queued burst settles on the last requested state");
        }
        std::cout << "100 bursts of 21 queued hotkeys settled correctly.\n";
        Check(WaitUntil([&] { return icon.IsSynchronized(); }), "notification icon converges to the latest state");
        std::cout << "Slowest notification-icon request: " << slowestIcon << " ms, background Shell call: "
            << icon.LongestCallMilliseconds() << " ms\n";
        std::cout << "Longest hotkey queue wait: " << LongestHotkeyQueueWait << " ms\n";
        Check(LongestHotkeyQueueWait < 100, "active capture must not stall queued shortcuts for 100 ms");
        if (useD3D && !IsZoomed(target))
        {
            RECT original{};
            GetWindowRect(target, &original);
            MONITORINFO monitor{sizeof(monitor)};
            Check(GetMonitorInfoW(MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE,
                "read test target monitor");
            SetWindowPos(target, nullptr, monitor.rcMonitor.left - 10, monitor.rcMonitor.top + 100,
                350, 240, SWP_NOACTIVATE | SWP_NOZORDER);
            SendMessageW(control, WM_HOTKEY, 1, 0);
            Check(WaitUntil([&] { return app.StatusText() == L"Compatibility - window extends beyond one display"; }),
                "automatic geometry fallback reaches active Magnification");
            SetWindowPos(target, nullptr, original.left, original.top, original.right - original.left,
                original.bottom - original.top, SWP_NOACTIVATE | SWP_NOZORDER);
            app.ReevaluateFocus();
            Check(WaitUntil([&] { return app.StatusText() == activeStatus; }), "fallback returns to D3D");
            Check(WaitUntil([&] { return OwnOverlay(L"CSP_GrayscaleOverlay") == nullptr; }),
                "returning to D3D releases Magnification windows asynchronously");
            SendMessageW(control, WM_HOTKEY, 1, 0);
            std::cout << "Automatic Magnification fallback and return to D3D passed.\n";
        }
        // Reenter the target with no focus notification, including after a
        // transient null foreground. Verify both registration and visible output.
        SendMessageW(control, WM_HOTKEY, 1, 0);
        Check(WaitUntil([&] { return app.StatusText() == activeStatus; }), "focus test starts active");
        for (int i = 0; i < 3; ++i)
        {
            foreground = nullptr;
            app.ReevaluateFocus();
            Check(!app.IsHotkeyRegistered(), "transient focus loss unregisters target-only shortcut");
            Check(WaitUntil([&] { return !IsWindowVisible(OwnOverlay(overlayClass)); }), "focus loss hides preview");
            foreground = target;
            Check(WaitUntil([&] { return app.IsHotkeyRegistered() && app.StatusText() == activeStatus; }, 1500),
                "focus recovery restores shortcut and preview without another event");
        }
        std::cout << "Three focus recoveries restored both shortcut and visible preview.\n";
        SendMessageW(control, WM_HOTKEY, 1, 0);
        Check(!app.IsGrayscaleEnabled(), "recovered shortcut still toggles off");
        app.SetStateCallback({});
    }

    void TestMaximizedApplication(HWND target)
    {
        MONITORINFO monitor{sizeof(monitor)};
        Check(GetMonitorInfoW(MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE,
            "read large test monitor");
        const SIZE border = csp::window::ResizeBorderForWindow(target);
        // Recreate the reported maximized custom-frame geometry on our own
        // no-activate test window. No user application or desktop setting changes.
        SetWindowLongPtrW(target, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_MAXIMIZE);
        SetWindowPos(target, nullptr, monitor.rcMonitor.left - border.cx, monitor.rcMonitor.top - border.cy,
            monitor.rcMonitor.right - monitor.rcMonitor.left + 2 * border.cx,
            monitor.rcMonitor.bottom - monitor.rcMonitor.top + 2 * border.cy,
            SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED);
        RECT raw{};
        Check(SUCCEEDED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &raw, sizeof(raw))),
            "read maximized test visual bounds");
        Check(IsZoomed(target) && !csp::d3d::CanCaptureWindow(raw, monitor.rcMonitor, DMDO_DEFAULT),
            "reproduce maximized bounds rejected by original capture policy");
        std::cout << "Maximized animated target: (" << raw.left << ',' << raw.top << ")-("
            << raw.right << ',' << raw.bottom << "), monitor "
            << monitor.rcMonitor.right - monitor.rcMonitor.left << 'x'
            << monitor.rcMonitor.bottom - monitor.rcMonitor.top << '\n';
        Check(SetTimer(target, 2, 17, nullptr) != 0, "animate large test source");
        TestApplicationToggles(target, true);
        TestApplicationToggles(target, false);
        KillTimer(target, 2);
    }

    void TestRevealPixels(HWND target)
    {
        // Sample one pixel inside this test-owned window only. Disabling capture
        // exclusion on our own overlay makes its composed output measurable.
        Check(SetWindowPos(target, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE, "bring test target above other windows");
        RECT bounds{};
        GetWindowRect(target, &bounds);
        const int x = (bounds.left + bounds.right) / 2;
        const int y = (bounds.top + bounds.bottom) / 2;
        std::cout << "Pixel target: (" << bounds.left << ',' << bounds.top << ")-(" << bounds.right << ','
            << bounds.bottom << "), dpi=" << GetDpiForWindow(target) << '\n';
        Check(WaitUntil([&] { return WindowFromPoint(POINT{x, y}) == target; }, 500),
            "pixel location belongs to the test window");
        csp::d3d::D3DOverlayRenderer renderer;
        Check(renderer.Initialize(), "pixel-test renderer initialization");
        const HWND overlay = OwnOverlay(L"CSP_D3DOverlay");
        Check(SetWindowDisplayAffinity(overlay, WDA_NONE) != FALSE, "allow observing our test overlay");
        renderer.Attach(target);
        HDC screen = GetDC(nullptr);
        HDC sample = CreateCompatibleDC(screen);
        HBITMAP pixelBitmap = CreateCompatibleBitmap(screen, 1, 1);
        const auto previousBitmap = SelectObject(sample, pixelBitmap);
        unsigned badPixels = 0, grayPixels = 0, samples = 0;
        COLORREF darkest = RGB(255, 255, 255);
        for (int cycle = 0; cycle < 24; ++cycle)
        {
            renderer.Hide();
            Check(WaitUntil([&] { return !IsWindowVisible(overlay); }), "pixel-test hide");
            InvalidateRect(target, nullptr, FALSE);
            UpdateWindow(target);
            PumpFor(60);
            DwmFlush();
            renderer.Show();
            const auto previousGray = grayPixels;
            const auto deadline = GetTickCount64() + 350;
            do
            {
                Check(WindowFromPoint(POINT{x, y}) == target, "sample remains inside the test window");
                Check(BitBlt(sample, 0, 0, 1, 1, screen, x, y, SRCCOPY | CAPTUREBLT) != FALSE,
                    "copy composed test pixel");
                const COLORREF pixel = GetPixel(sample, 0, 0);
                Check(pixel != CLR_INVALID, "read test pixel");
                const int r = GetRValue(pixel), g = GetGValue(pixel), b = GetBValue(pixel);
                const int luminance = (299 * r + 587 * g + 114 * b) / 1000;
                if (luminance < 102 || luminance > 122)
                {
                    if (badPixels < 12) std::cout << "Unexpected pixel: cycle=" << cycle
                        << " rgb=" << r << ',' << g << ',' << b << " active=" << renderer.IsVisible()
                        << " opacity=" << static_cast<int>(Opacity(overlay)) << '\n';
                    ++badPixels;
                }
                if (r == g && g == b && r > 100 && r < 123) ++grayPixels;
                if (r + g + b < GetRValue(darkest) + GetGValue(darkest) + GetBValue(darkest)) darkest = pixel;
                ++samples;
                PumpFor(1);
            } while (GetTickCount64() < deadline && !renderer.HasFailed());
            Check(!renderer.HasFailed(), "pixel-test remains on D3D");
            Check(renderer.IsVisible() && grayPixels > previousGray, "every reveal produces visible grayscale pixels");
        }
        SelectObject(sample, previousBitmap);
        DeleteObject(pixelBitmap);
        DeleteDC(sample);
        ReleaseDC(nullptr, screen);
        std::cout << "Reveal pixel samples=" << samples << " unexpected=" << badPixels << " gray=" << grayPixels
            << " darkest=" << static_cast<int>(GetRValue(darkest)) << ',' << static_cast<int>(GetGValue(darkest))
            << ',' << static_cast<int>(GetBValue(darkest)) << '\n';
        Check(grayPixels > 0, "pixel test actually observes grayscale output");
        Check(badPixels == 0, "reveal must not display a black or corrupted frame");
    }

    void TestRevealOnDisplays(HWND target, bool maximizedOnly = false)
    {
        std::vector<RECT> displays;
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR, HDC, LPRECT rect, LPARAM data) -> BOOL
        {
            reinterpret_cast<std::vector<RECT>*>(data)->push_back(*rect);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&displays));
        Check(!displays.empty(), "enumerate displays for reveal tests");
        for (const auto& monitor : displays)
        {
            if (maximizedOnly) break;
            std::cout << "Reveal on display: (" << monitor.left << ',' << monitor.top << ")-("
                << monitor.right << ',' << monitor.bottom << ")\n";
            HWND displayTarget = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                L"CSP_RendererTestTarget", L"Renderer pixel test", WS_POPUP,
                monitor.left + 80, monitor.top + 80, 320, 220, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            Check(displayTarget != nullptr, "create per-display pixel target");
            ShowWindow(displayTarget, SW_SHOWNOACTIVATE);
            UpdateWindow(displayTarget);
            PumpFor(60);
            DwmFlush();
            try { TestRevealPixels(displayTarget); }
            catch (...) { DestroyWindow(displayTarget); throw; }
            DestroyWindow(displayTarget);
        }
        // Give the maximized sample its own initially-topmost window too.
        // Reusing the earlier window can leave it behind other windows even
        // after SetWindowPos reports success when promoting it to topmost.
        MONITORINFO monitor{sizeof(monitor)};
        Check(GetMonitorInfoW(MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE,
            "read maximized pixel-test monitor");
        const SIZE border = csp::window::ResizeBorderForWindow(target);
        HWND maximizedTarget = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
            L"CSP_RendererTestTarget", L"Maximized pixel test", WS_POPUP | WS_VISIBLE | WS_MAXIMIZE,
            monitor.rcMonitor.left - border.cx, monitor.rcMonitor.top - border.cy,
            monitor.rcMonitor.right - monitor.rcMonitor.left + 2 * border.cx,
            monitor.rcMonitor.bottom - monitor.rcMonitor.top + 2 * border.cy,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(maximizedTarget != nullptr, "create maximized pixel target");
        Check(IsZoomed(maximizedTarget) != FALSE, "pixel target is maximized");
        UpdateWindow(maximizedTarget);
        PumpFor(60);
        DwmFlush();
        std::cout << "Reveal on maximized target\n";
        try { TestRevealPixels(maximizedTarget); }
        catch (...) { DestroyWindow(maximizedTarget); throw; }
        DestroyWindow(maximizedTarget);
    }
}
int main(int argc, char** argv)
{
    bool profile = false, maximizedOnly = false, revealOnly = false, revealMaximizedOnly = false;
    for (int i = 1; i < argc; ++i)
    {
        profile |= std::string(argv[i]) == "--profile";
        maximizedOnly |= std::string(argv[i]) == "--maximized";
        revealOnly |= std::string(argv[i]) == "--reveal";
        revealMaximizedOnly |= std::string(argv[i]) == "--reveal-maximized";
    }
    if (profile)
    {
        csp::util::Logger::Instance().SetLevel(csp::util::Logger::LogLevel::Debug);
        csp::util::Logger::Instance().SetSink([](const std::wstring& line)
        {
            if (line.find(L"[INFO]") != std::wstring::npos) return;
            const int size = WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                nullptr, 0, nullptr, nullptr);
            std::string utf8(size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                utf8.data(), size, nullptr, nullptr);
            std::cout << utf8;
        });
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 1;
    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"CSP_RendererTestTarget";
    wc.lpfnWndProc = TargetProc;
    RegisterClassW(&wc);
    HWND target = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName,
        L"Renderer integration test", WS_POPUP, 100, 100, 320, 220, nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(target, SW_SHOWNOACTIVATE);
    UpdateWindow(target);
    int result = 0;
    try
    {
        Check(target != nullptr, "create test target");
        if (revealOnly || revealMaximizedOnly) TestRevealOnDisplays(target, revealMaximizedOnly);
        else
        {
            if (!maximizedOnly)
            {
                TestD3D(target);
                TestMagnification(target);
                TestApplicationToggles(target, true);
                TestApplicationToggles(target, false);
            }
            TestMaximizedApplication(target);
            SetWindowLongPtrW(target, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            TestRevealOnDisplays(target);
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAILED: " << error.what() << '\n';
        const auto history = csp::util::Logger::Instance().History();
        const size_t begin = history.size() > 100 ? history.size() - 100 : 0;
        for (size_t i = begin; i < history.size(); ++i) std::wcerr << history[i];
        result = 1;
    }
    DestroyWindow(target);
    CoUninitialize();
    return result;
}
