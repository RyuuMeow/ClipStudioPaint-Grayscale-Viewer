#include "d3d/D3DOverlayRenderer.h"
#include "magnification/MagnifierOverlay.h"
#include "util/Logger.h"
#include "core/Application.h"
#include <Windows.h>
#include <dwmapi.h>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <shellapi.h>

namespace
{
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
        if (msg == WM_PAINT)
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            RECT rect{};
            GetClientRect(hwnd, &rect);
            HBRUSH brush = CreateSolidBrush(RGB(220, 60, 100));
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
            Check(WaitUntil([&]
            {
                return IsWindowVisible(overlay) && Opacity(overlay) == 1;
            }), "D3D primes the first frame without exposing an opaque blank window");
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
        HWND overlay = OwnOverlay(L"CSP_GrayscaleOverlay");
        Check(overlay && renderer.IsVisible() && IsWindowVisible(overlay), "Magnification first show");
        for (int i = 0; i < 20; ++i)
        {
            renderer.Hide();
            // Reproduce a WM_TIMER already queued before KillTimer.
            SendMessageW(overlay, WM_TIMER, 1, 0);
            PumpFor(20);
            Check(!IsWindowVisible(overlay) && !renderer.IsVisible(), "stale Mag timer cannot reshow hidden overlay");
            renderer.Show();
            PumpFor(50);
            Check(renderer.IsVisible() && IsWindowVisible(overlay) && !renderer.HasFailed(), "Mag resumes at unchanged bounds");
        }
        renderer.Shutdown();
        Check(!IsWindow(overlay), "Magnification shutdown destroys overlay");
        Check(renderer.Initialize(), "Magnification reinitialization");
        renderer.Attach(target);
        renderer.Show();
        overlay = OwnOverlay(L"CSP_GrayscaleOverlay");
        Check(renderer.IsVisible() && IsWindowVisible(overlay), "Magnification restart at unchanged bounds");
        renderer.Shutdown();
        std::cout << "Magnification: 20 hide/show cycles, stale timers and restart passed.\n";
    }
    void TestApplicationToggles(HWND target)
    {
        // Exercise the real hotkey-to-application path using only this test's windows.
        wchar_t executable[MAX_PATH]{};
        GetModuleFileNameW(nullptr, executable, _countof(executable));
        csp::core::Settings settings;
        settings.TargetApps = {std::filesystem::path(executable).filename().wstring()};
        settings.ToggleHotkey = {MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F24};
        settings.UseD3D = true;
        // Keep real desktop focus untouched; only this application's foreground lookup
        // sees the test-owned target. OS keyboard delivery is a separate manual check.
        csp::core::Application app([target] { return target; });
        Check(app.Initialize(settings) && app.IsHotkeyRegistered(), "application test shortcut registered");
        HWND control = nullptr;
        while ((control = FindWindowExW(HWND_MESSAGE, control, L"CSP_GrayscaleControl", nullptr)))
        {
            DWORD process = 0;
            GetWindowThreadProcessId(control, &process);
            if (process == GetCurrentProcessId()) break;
        }
        Check(control != nullptr, "find own application control window");
        struct TestIcon
        {
            NOTIFYICONDATAW Data{};
            ~TestIcon() { Shell_NotifyIconW(NIM_DELETE, &Data); }
        } icon;
        icon.Data.cbSize = sizeof(icon.Data);
        icon.Data.hWnd = control;
        icon.Data.uID = 37;
        icon.Data.uFlags = NIF_ICON | NIF_TIP;
        icon.Data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcscpy_s(icon.Data.szTip, L"CSP toggle integration test");
        Check(Shell_NotifyIconW(NIM_ADD, &icon.Data) != FALSE, "create own test notification icon");
        double slowestIcon = 0;
        app.SetStateCallback([&](bool, bool)
        {
            icon.Data.uFlags = NIF_TIP | NIF_SHOWTIP;
            wcsncpy_s(icon.Data.szTip, app.StatusText().c_str(), _TRUNCATE);
            const auto start = std::chrono::steady_clock::now();
            Shell_NotifyIconW(NIM_MODIFY, &icon.Data);
            slowestIcon = (std::max)(slowestIcon, std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count());
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
        Check(WaitUntil([&] { return app.StatusText() == L"Active - Direct3D"; }),
            "rapid toggles settle on the last requested D3D state");
        double resumeTotal = 0;
        for (int i = 0; i < 10; ++i)
        {
            SendMessageW(control, WM_HOTKEY, 1, 0);
            const HWND ownOverlay = OwnOverlay(L"CSP_D3DOverlay");
            Check(WaitUntil([&] { return !IsWindowVisible(ownOverlay); }), "application hides between tap measurements");
            const auto resume = std::chrono::steady_clock::now();
            SendMessageW(control, WM_HOTKEY, 1, 0);
            Check(WaitUntil([&] { return app.StatusText() == L"Active - Direct3D"; }), "each tap becomes visible");
            resumeTotal += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - resume).count();
        }
        std::cout << "10 short-pause toggles, mean activation: " << resumeTotal / 10 << " ms\n";
        SendMessageW(control, WM_HOTKEY, 1, 0);
        Check(!app.IsGrayscaleEnabled(), "final hotkey turns preview off");
        const HWND overlay = OwnOverlay(L"CSP_D3DOverlay");
        Check(WaitUntil([&] { return !IsWindowVisible(overlay); }), "rapid toggles settle off");
        for (int batch = 0; batch < 100; ++batch)
        {
            for (int i = 0; i < 21; ++i)
                Check(PostMessageW(control, WM_HOTKEY, 1, 0) != FALSE, "queue own hotkey burst");
            const bool expectedOn = batch % 2 == 0;
            Check(WaitUntil([&]
            {
                return app.IsGrayscaleEnabled() == expectedOn &&
                    (expectedOn ? app.StatusText() == L"Active - Direct3D" : !IsWindowVisible(overlay));
            }), "queued burst settles on the last requested state");
        }
        std::cout << "100 bursts of 21 queued hotkeys settled correctly.\n";
        std::cout << "Slowest notification-icon update: " << slowestIcon << " ms\n";
        app.SetStateCallback({});
    }
}
int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "--profile")
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
        TestD3D(target);
        TestMagnification(target);
        TestApplicationToggles(target);
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAILED: " << error.what() << '\n';
        result = 1;
    }
    DestroyWindow(target);
    CoUninitialize();
    return result;
}
