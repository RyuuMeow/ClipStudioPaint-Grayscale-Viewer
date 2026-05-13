#pragma once

#include "d3d/D3DOverlayRenderer.h"
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

        void SetD3DRendererEnabled(bool Enabled);
        bool IsD3DRendererEnabled() const { return bUseD3DRenderer; }

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

        void AttachActiveOverlay(HWND Hwnd);
        void DetachActiveOverlay();
        void HideActiveOverlay();
        void ShowActiveOverlay();
        HWND ActiveOverlayTargetWindow() const;
        bool IsActiveOverlayVisible() const;

        void NotifyState();

        magnification::MagnifierOverlay MagOverlay;
        d3d::D3DOverlayRenderer D3DOverlay;
        window::FocusTracker FocusTracker;
        input::HotkeyManager HotkeyManager;

        bool bIsGrayscaleEnabled = false;
        bool bIsTargetInFocus = false;
        bool bHotkeyFocusOnly = true;
        bool bHotkeyRegistered = false;
        bool bUseD3DRenderer = false;
        int RefreshRate = 60;

        UINT HotkeyMod = 0;
        UINT HotkeyVk = VK_F9;

        StateCallback StateChangeCallback;
    };
}
