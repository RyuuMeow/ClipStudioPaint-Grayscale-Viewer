#include "core/Application.h"
#include "d3d/RenderPolicy.h"
#include "window/WindowGeometry.h"
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
        constexpr UINT_PTR kFocusWatchdog = 2;
        constexpr UINT kFocusCheckMs = 250;
        constexpr ULONGLONG kFocusRetryMs = 1000;
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
        if (!SetTimer(ControlWindow, kFocusWatchdog, kFocusCheckMs, nullptr))
        {
            LOG_ERROR(L"Cannot start focus recovery timer (error %u)", GetLastError());
            return false;
        }
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
            const ULONGLONG dispatchStarted = GetTickCount64();
            DispatchMessageW(&msg);
            const ULONGLONG dispatchMs = GetTickCount64() - dispatchStarted;
            if (dispatchMs >= 50)
                LOG_WARNING(L"UI message 0x%04X took %llu ms", msg.message, dispatchMs);
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
        RefreshFocus(false);
    }
    void Application::RefreshFocus(bool periodic)
    {
        const HWND foreground = ReadForeground();
        HWND root = foreground ? GetAncestor(foreground, GA_ROOTOWNER) : nullptr;
        if (!root) root = foreground;
        const bool usable = root && IsWindowVisible(root) && !IsIconic(root);
        const ULONGLONG now = GetTickCount64();
        const bool changed = foreground != LastForeground || root != LastRoot || usable != LastRootUsable;
        const bool retryResolution = !Suspended && foreground && !FocusedTarget &&
            now - LastFocusCheck >= kFocusRetryMs;
        const bool retryShortcut = HotkeyConflict && now - LastHotkeyAttempt >= kFocusRetryMs;
        if (periodic && !changed && !retryResolution)
        {
            // Stable focus needs no process lookup, renderer selection or driver work.
            if (retryShortcut)
            {
                HotkeyConflict = false;
                SyncHotkey(FocusedTarget != nullptr);
                NotifyState();
            }
            return;
        }
        LastForeground = foreground;
        LastRoot = root;
        LastRootUsable = usable;
        LastFocusCheck = now;
        HWND target = nullptr;
        if (!Suspended && usable && window::WindowFinder::IsTargetWindow(foreground)) target = root;
        const HWND previous = FocusedTarget;
        FocusedTarget = target;
        if (retryShortcut) HotkeyConflict = false;
        if (previous != target)
        {
            LOG_INFO(L"Focus target changed: foreground=%p, target=%p, source=%s",
                foreground, target, periodic ? L"periodic recovery" : L"event or command");
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
        const ULONGLONG syncStarted = GetTickCount64();
        SyncOverlay(target);
        const ULONGLONG syncMs = GetTickCount64() - syncStarted;
        if (syncMs >= 50) LOG_WARNING(L"Renderer selection took %llu ms", syncMs);
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
            LastHotkeyAttempt = GetTickCount64();
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
        const HRESULT boundsResult = DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect));
        if (FAILED(boundsResult) && !GetWindowRect(target, &rect)) return;
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        const HMONITOR monitor = MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
        const bool monitorInfoKnown = GetMonitorInfoW(monitor, &info) != FALSE;
        const bool modeKnown = monitorInfoKnown && EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode);
        const bool monitorKnown = monitorInfoKnown && modeKnown;
        const RECT captureRect = monitorInfoKnown ? window::VisibleCaptureBounds(target, rect, info.rcMonitor) : rect;
        const int monitorRate = monitorKnown ? static_cast<int>(mode.dmDisplayFrequency) : 60;
        bool useD3D = PreferD3D && !D3DFailed && monitorKnown &&
            d3d::CanCaptureWindow(captureRect, info.rcMonitor, mode.dmDisplayOrientation);
        FallbackReason = !PreferD3D ? L"" : D3DFailed ? L"capture unavailable" :
            !monitorKnown ? L"display information unavailable" :
            mode.dmDisplayOrientation != DMDO_DEFAULT ? L"rotated display" :
            !useD3D ? L"window extends beyond one display" : L"";
        Backend desired = useD3D ? Backend::Direct3D : Backend::Magnification;
        if (CurrentBackend != desired && !EqualRect(&rect, &captureRect))
            LOG_INFO(L"Maximized border excluded: bounds=(%ld,%ld)-(%ld,%ld), capture=(%ld,%ld)-(%ld,%ld)",
                rect.left, rect.top, rect.right, rect.bottom,
                captureRect.left, captureRect.top, captureRect.right, captureRect.bottom);
        if (CurrentBackend != desired && !FallbackReason.empty())
        {
            LOG_INFO(L"Magnification fallback: %s; target=(%ld,%ld)-(%ld,%ld), monitor=(%ld,%ld)-(%ld,%ld), orientation=%u",
                FallbackReason.c_str(), rect.left, rect.top, rect.right, rect.bottom,
                info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom,
                mode.dmDisplayOrientation);
            LOG_INFO(L"Fallback geometry: bounds=%s, maximized=%d, monitorInfo=%d, displayMode=%d, device=%s, modeFields=0x%08X",
                SUCCEEDED(boundsResult) ? L"DWM" : L"GetWindowRect", IsZoomed(target), monitorInfoKnown,
                modeKnown, info.szDevice, mode.dmFields);
        }
        if (CurrentBackend != desired) HideOverlays();
        if (useD3D)
        {
            MagOverlay.ReleaseResources();
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
                if (self->CurrentBackend == Backend::None) self->MagOverlay.ReleaseResources();
                return 0;
            }
            if (msg == WM_TIMER && wp == kFocusWatchdog)
            {
                self->RefreshFocus(true);
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}
