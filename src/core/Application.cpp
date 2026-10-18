#include "core/Application.h"
#include "d3d/RenderPolicy.h"
#include "util/Logger.h"
#include <algorithm>
#include <dwmapi.h>

namespace csp::core
{
    namespace
    {
        constexpr UINT kFocusUpdate = WM_APP + 10;
        constexpr UINT kRendererUpdate = WM_APP + 11;
        constexpr UINT_PTR kReleaseIdle = 1;
        constexpr wchar_t kControlClass[] = L"CSP_GrayscaleControl";
    }

    Application::~Application()
    {
        StateChangeCallback = {};
        FocusTracker.Stop();
        HotkeyManager.UnregisterAll();
        D3DOverlay.Shutdown();
        MagOverlay.Shutdown();
        if (ControlWindow) DestroyWindow(ControlWindow);
    }

    bool Application::Initialize(const Settings& initial)
    {
        RefreshRate = initial.RefreshRate;
        ToggleKey = initial.ToggleHotkey;
        FocusOnly = initial.FocusOnly;
        PreferD3D = initial.UseD3D;
        window::WindowFinder::SetTargetProcessNames(initial.TargetApps);
        WNDCLASSW wc{};
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpfnWndProc = ControlWndProc;
        wc.lpszClassName = kControlClass;
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        ControlWindow = CreateWindowExW(0, kControlClass, L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, wc.hInstance, this);
        if (!ControlWindow) return false;
        HotkeyManager.SetWindow(ControlWindow);
        D3DOverlay.SetNotificationWindow(ControlWindow, kRendererUpdate);
        MagOverlay.SetNotificationWindow(ControlWindow, kRendererUpdate);
        if (!FocusTracker.Start([this](HWND, bool) { QueueFocusUpdate(); })) return false;
        ReevaluateFocus();
        LOG_INFO(L"Native tray core initialized; renderers start on demand");
        return true;
    }

    int Application::RunConsole()
    {
        MSG msg{};
        BOOL result;
        while ((result = GetMessageW(&msg, nullptr, 0, 0)) > 0)
        {
            if (msg.message == WM_HOTKEY)
            {
                const DWORD delay = GetTickCount() - msg.time;
                if (delay >= 50) LOG_WARNING(L"Shortcut waited %u ms in the message queue", delay);
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return result < 0 ? 1 : static_cast<int>(msg.wParam);
    }

    void Application::Quit() { PostQuitMessage(0); }
    void Application::HandleNativeHotkey(int id)
    {
        LOG_INFO(L"Shortcut received #%llu: id=%d, registered=%s", ++HotkeyEvents, id,
            HotkeyRegistered ? L"yes" : L"no");
        HotkeyManager.HandleHotkey(id);
    }
    void Application::ToggleGrayscale()
    {
        Enabled = !Enabled;
        OverlayError = false;
        ReevaluateFocus();
    }
    void Application::SetRefreshRate(int fps)
    {
        RefreshRate = fps == 0 ? 0 : std::clamp(fps, 15, 144);
        ReevaluateFocus();
    }
    void Application::SetD3DRendererEnabled(bool value)
    {
        PreferD3D = value;
        D3DFailed = false;
        OverlayError = false;
        if (!value || D3DOverlay.HasFailed()) D3DOverlay.Shutdown();
        ReevaluateFocus();
    }
    bool Application::SetHotkey(UINT modifiers, UINT vk)
    {
        const input::Hotkey proposed{modifiers, vk};
        if (!input::IsSupportedHotkey(proposed)) return false;
        if (proposed == ToggleKey && HotkeyRegistered) return true;
        const int newId = HotkeyId == 1 ? 2 : 1;
        // Register the replacement before releasing the working binding.
        if (!HotkeyManager.Register(newId, modifiers, vk, [this] { ToggleGrayscale(); })) return false;
        if (HotkeyRegistered) HotkeyManager.Unregister(HotkeyId);
        ToggleKey = proposed;
        HotkeyId = newId;
        HotkeyRegistered = true;
        HotkeyConflict = false;
        SyncHotkey(FocusedTarget != nullptr);
        NotifyState();
        return true;
    }
    void Application::SetHotkeyFocusOnly(bool value)
    {
        FocusOnly = value;
        ReevaluateFocus();
    }
    void Application::SetTargetApps(const std::vector<std::wstring>& apps)
    {
        window::WindowFinder::SetTargetProcessNames(apps);
        ReevaluateFocus();
    }
    void Application::DisplayConfigurationChanged()
    {
        D3DFailed = false;
        HideOverlays();
        D3DOverlay.Shutdown();
        ReevaluateFocus();
    }
    void Application::SetSuspended(bool value)
    {
        Suspended = value;
        if (value) D3DOverlay.Shutdown();
        if (!value) D3DFailed = false;
        ReevaluateFocus();
    }
    void Application::QueueFocusUpdate()
    {
        if (!FocusUpdatePending)
        {
            FocusUpdatePending = PostMessageW(ControlWindow, kFocusUpdate, 0, 0) != FALSE;
        }
    }
    void Application::ReevaluateFocus()
    {
        HWND foreground = ReadForeground();
        HWND target = nullptr;
        if (!Suspended && foreground && window::WindowFinder::IsTargetWindow(foreground))
        {
            target = GetAncestor(foreground, GA_ROOTOWNER);
            if (!target) target = foreground;
            if (IsIconic(target) || !IsWindowVisible(target)) target = nullptr;
        }
        const HWND previous = FocusedTarget;
        FocusedTarget = target;
        if (previous != target)
        {
            HotkeyConflict = false;
            // A new focus session can retry a transient capture failure once.
            if (target && D3DFailed)
            {
                D3DOverlay.Shutdown();
                D3DFailed = false;
            }
            OverlayError = false;
        }
        SyncHotkey(target != nullptr);
        SyncOverlay(target);
        NotifyState();
    }
    void Application::SyncHotkey(bool focused)
    {
        const bool wanted = !Suspended && (!FocusOnly || focused);
        if (!wanted && HotkeyRegistered)
        {
            HotkeyManager.UnregisterAll();
            HotkeyRegistered = false;
        }
        else if (wanted && !HotkeyRegistered && !HotkeyConflict)
        {
            HotkeyRegistered = HotkeyManager.Register(HotkeyId, ToggleKey.Modifiers, ToggleKey.Key,
                [this] { ToggleGrayscale(); });
            HotkeyConflict = !HotkeyRegistered;
        }
    }
    void Application::HideOverlays()
    {
        MagOverlay.Detach();
        D3DOverlay.Detach();
        CurrentBackend = Backend::None;
    }
    void Application::SyncOverlay(HWND target)
    {
        if (!Enabled || !target)
        {
            const bool hadOverlay = CurrentBackend != Backend::None;
            HideOverlays();
            FallbackReason.clear();
            if (hadOverlay) SetTimer(ControlWindow, kReleaseIdle, 5000, nullptr);
            return;
        }
        KillTimer(ControlWindow, kReleaseIdle);
        RECT rect{};
        if (FAILED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))) &&
            !GetWindowRect(target, &rect)) return;
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        const HMONITOR monitor = MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
        const bool monitorKnown = GetMonitorInfoW(monitor, &info) &&
            EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode);
        const int monitorRate = monitorKnown ? static_cast<int>(mode.dmDisplayFrequency) : 60;
        bool useD3D = PreferD3D && !D3DFailed && monitorKnown &&
            d3d::CanCaptureWindow(rect, info.rcMonitor, mode.dmDisplayOrientation);
        FallbackReason = !PreferD3D ? L"" : D3DFailed ? L"capture unavailable" :
            !useD3D ? L"multiple screens or rotated display" : L"";
        Backend desired = useD3D ? Backend::Direct3D : Backend::Magnification;
        if (CurrentBackend != desired) HideOverlays();
        if (useD3D)
        {
            MagOverlay.Shutdown();
            D3DOverlay.SetRefreshRate(d3d::EffectiveRefreshRate(RefreshRate, monitorRate, false));
            if (D3DOverlay.Initialize())
            {
                D3DOverlay.Attach(target);
                D3DOverlay.Show();
                CurrentBackend = desired;
                return;
            }
            D3DFailed = true;
            FallbackReason = L"capture unavailable";
            desired = Backend::Magnification;
        }
        D3DOverlay.Hide();
        if (OverlayError) return;
        MagOverlay.SetRefreshRate(d3d::EffectiveRefreshRate(RefreshRate, monitorRate, true));
        if (!MagOverlay.Initialize())
        {
            OverlayError = true;
            return;
        }
        if (MagOverlay.TargetWindow() != target) MagOverlay.Hide();
        MagOverlay.Attach(target);
        MagOverlay.Show();
        CurrentBackend = desired;
    }
    bool Application::Active() const
    {
        if (!Enabled || !FocusedTarget) return false;
        return CurrentBackend == Backend::Direct3D ? D3DOverlay.IsVisible() :
            CurrentBackend == Backend::Magnification && MagOverlay.IsVisible();
    }
    std::wstring Application::StatusText() const
    {
        if (HotkeyConflict) return L"Shortcut unavailable - choose another key";
        if (Suspended) return L"Paused";
        if (!Enabled) return L"Off";
        if (OverlayError) return L"Preview unavailable - see diagnostics";
        if (!FocusedTarget) return L"Waiting for a target application";
        if (!Active()) return L"Starting preview";
        if (CurrentBackend == Backend::Direct3D) return L"Active - Direct3D";
        return FallbackReason.empty() ? L"Active - Magnification" : L"Compatibility - " + FallbackReason;
    }
    void Application::ForceNotifyState() { NotifyState(true); }
    void Application::NotifyState(bool force)
    {
        const bool active = Active();
        const auto text = StatusText();
        if (!force && LastEnabled == Enabled && LastActive == active && LastStatus == text) return;
        LastEnabled = Enabled;
        LastActive = active;
        LastStatus = text;
        LOG_INFO(L"State: %s", text.c_str());
        if (StateChangeCallback) StateChangeCallback(Enabled, active);
    }
    LRESULT CALLBACK Application::ControlWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        auto* self = reinterpret_cast<Application*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE)
        {
            self = static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self)
        {
            if (msg == WM_HOTKEY) { self->HandleNativeHotkey(static_cast<int>(wp)); return 0; }
            if (msg == kFocusUpdate)
            {
                self->FocusUpdatePending = false;
                self->ReevaluateFocus();
                return 0;
            }
            if (msg == kRendererUpdate)
            {
                if (self->CurrentBackend == Backend::Magnification && self->MagOverlay.HasFailed())
                    self->OverlayError = true;
                if (self->D3DOverlay.HasFailed() && !self->D3DFailed)
                {
                    self->D3DFailed = true;
                    self->SyncOverlay(self->FocusedTarget);
                }
                self->NotifyState();
                return 0;
            }
            if (msg == WM_TIMER && wp == kReleaseIdle)
            {
                KillTimer(hwnd, kReleaseIdle);
                if (self->CurrentBackend == Backend::None) self->MagOverlay.Shutdown();
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}
