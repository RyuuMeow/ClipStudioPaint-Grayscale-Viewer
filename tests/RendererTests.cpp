#include "d3d/D3DOverlayRenderer.h"
#include "magnification/MagnifierOverlay.h"
#include <Windows.h>
#include <dwmapi.h>
#include <iostream>
#include <stdexcept>

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
        renderer.Attach(target);
        renderer.SetRefreshRate(60);
        renderer.Show();
        Check(WaitUntil([&] { return renderer.IsVisible() || renderer.HasFailed(); }), "D3D first-frame timeout");
        Check(renderer.IsVisible() && !renderer.HasFailed(), "D3D first frame (requires unlocked GPU desktop)");
        const HWND overlay = OwnOverlay(L"CSP_D3DOverlay");
        Check(overlay && IsWindowVisible(overlay), "D3D overlay really visible");
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
        }
        renderer.Detach();
        Check(WaitUntil([&] { return !renderer.HasDeviceResources(); }, 6500), "D3D frees idle GPU resources");
        Check(!IsWindowVisible(overlay), "D3D stays hidden after resource release");
        renderer.Attach(target);
        renderer.Show();
        Check(WaitUntil([&] { return renderer.IsVisible() || renderer.HasFailed(); }), "D3D restarts after resource release");
        Check(!renderer.HasFailed(), "D3D restart remains healthy");
        renderer.Shutdown();
        Check(!IsWindow(overlay) && !renderer.HasDeviceResources(), "D3D shutdown releases window and device");
        std::cout << "D3D: first frame, resize, 20 hide/show cycles, idle release and restart passed.\n";
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
}
int main()
{
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
