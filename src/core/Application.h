#pragma once

#include "magnification/MagnifierOverlay.h"
#include "window/FocusTracker.h"
#include "window/WindowFinder.h"
#include "input/HotkeyManager.h"

#include <functional>
#include <vector>
#include <string>

namespace csp::core
{
    class Application
    {
    public:
        Application() = default;

        ~Application();

        Application(const Application&) = delete;

        Application& operator=(const Application&) = delete;

        bool Initialize();

        int RunConsole();

        void Quit();

        void ToggleGrayscale();

        bool IsGrayscaleEnabled() const
        { return bIsGrayscaleEnabled; }

        // === Settings ===
        void SetRefreshRate(int Fps);

        void SetHotkey(UINT Modifiers, UINT Vk);

        void SetHotkeyFocusOnly(bool Enabled);

        bool IsHotkeyFocusOnly() const { return bHotkeyFocusOnly; }

        void SetTargetApps(const std::vector<std::wstring>& Apps);

        void HandleNativeHotkey(int Id);

        void ReevaluateFocus();

        // === Callbacks ===
        using StateCallback = std::function<void(bool Enabled, bool Active)>;
        void SetStateCallback(StateCallback Cb) { StateChangeCallback = std::move(Cb); }

    private:
        void OnFocusChanged(HWND Hwnd, bool IsTarget);

        void RegisterHotkeyNow();
        void UnregisterHotkeyNow();

        void NotifyState();

        magnification::MagnifierOverlay MagOverlay;
        window::FocusTracker FocusTracker;
        input::HotkeyManager HotkeyManager;

        bool bIsGrayscaleEnabled = false;
        bool bIsTargetInFocus = false;
        bool bHotkeyFocusOnly = true;
        bool bHotkeyRegistered = false;

        UINT HotkeyMod = 0;
        UINT HotkeyVk = VK_F9;

        StateCallback StateChangeCallback;
    };
}
