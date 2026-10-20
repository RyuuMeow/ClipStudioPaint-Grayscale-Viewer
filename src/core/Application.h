#pragma once
#include "core/Settings.h"
#include "d3d/D3DOverlayRenderer.h"
#include "magnification/MagnifierOverlay.h"
#include "window/FocusTracker.h"
#include "window/WindowFinder.h"
#include "input/HotkeyManager.h"
#include <functional>

namespace csp::core
{
    class Application
    {
    public:
        explicit Application(std::function<HWND()> ReadForeground = GetForegroundWindow)
            : ReadForeground(std::move(ReadForeground)) {}
        ~Application();
        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        bool Initialize(const Settings& Initial = {});
        int RunConsole();
        void Quit();
        void ToggleGrayscale();
        bool IsGrayscaleEnabled() const { return Enabled; }
        void SetRefreshRate(int Fps);
        void SetD3DRendererEnabled(bool Value);
        bool IsD3DRendererEnabled() const { return PreferD3D; }
        bool SetHotkey(UINT Modifiers, UINT Vk);
        void SetHotkeyFocusOnly(bool Value);
        bool IsHotkeyFocusOnly() const { return FocusOnly; }
        bool IsHotkeyRegistered() const { return HotkeyRegistered; }
        void SetTargetApps(const std::vector<std::wstring>& Apps);
        void HandleNativeHotkey(int Id);
        void ReevaluateFocus();
        void ForceNotifyState();
        void DisplayConfigurationChanged();
        void SetSuspended(bool Value);
        std::wstring StatusText() const;
        using StateCallback = std::function<void(bool Enabled, bool Active)>;
        void SetStateCallback(StateCallback Cb) { StateChangeCallback = std::move(Cb); }
    private:
        enum class Backend { None, Magnification, Direct3D };
        static LRESULT CALLBACK ControlWndProc(HWND, UINT, WPARAM, LPARAM);
        void QueueFocusUpdate();
        void RefreshFocus(bool Periodic);
        void SyncOverlay(HWND Target);
        void HideOverlays();
        bool Active() const;
        void NotifyState(bool Force = false);
        void SyncHotkey(bool TargetFocused);
        magnification::MagnifierOverlay MagOverlay;
        d3d::D3DOverlayRenderer D3DOverlay;
        window::FocusTracker FocusTracker;
        input::HotkeyManager HotkeyManager;
        std::function<HWND()> ReadForeground;
        HWND ControlWindow = nullptr;
        HWND FocusedTarget = nullptr;
        HWND LastForeground = nullptr;
        HWND LastRoot = nullptr;
        bool LastRootUsable = false;
        ULONGLONG LastFocusCheck = 0;
        ULONGLONG LastHotkeyAttempt = 0;
        Backend CurrentBackend = Backend::None;
        bool Enabled = false;
        bool FocusOnly = true;
        bool PreferD3D = false;
        bool Suspended = false;
        bool HotkeyRegistered = false;
        bool HotkeyConflict = false;
        bool FocusUpdatePending = false;
        bool D3DFailed = false;
        bool OverlayError = false;
        bool LastEnabled = false;
        bool LastActive = false;
        std::wstring LastStatus;
        std::wstring FallbackReason;
        int RefreshRate = 60;
        int HotkeyId = 1;
        uint64_t HotkeyEvents = 0;
        input::Hotkey ToggleKey;
        StateCallback StateChangeCallback;
    };
}
