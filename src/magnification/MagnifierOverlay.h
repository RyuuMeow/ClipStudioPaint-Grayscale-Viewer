#pragma once

#include <Windows.h>
#include <magnification.h>

#pragma comment(lib, "Magnification.lib")

namespace csp::magnification
{
    class MagnifierOverlay
    {
    public:
        MagnifierOverlay() = default;
        ~MagnifierOverlay();

        MagnifierOverlay(const MagnifierOverlay&) = delete;
        MagnifierOverlay& operator=(const MagnifierOverlay&) = delete;

        /// Initialize the Magnification API and create overlay windows.
        bool Initialize();

        /// Destroy windows and uninitialize Magnification API.
        void Shutdown();

        /// Attach to a target window (e.g. CSP).
        void Attach(HWND targetWindow);

        /// Detach from the target window and hide overlay.
        void Detach();

        /// Show the grayscale overlay on top of the target window.
        void Show();

        /// Hide the grayscale overlay.
        void Hide();

        /// Change the refresh rate (recreates timer if active).
        void SetRefreshRate(int fps);

        bool IsVisible() const
        { return Visible; }
        bool IsInitialized() const
        { return Initialized; }
        HWND TargetWindow() const
        { return TargetWnd; }

    private:
        static LRESULT CALLBACK HostWndProc(HWND, UINT, WPARAM, LPARAM);

        bool CreateHostWindow();
        bool CreateMagnifierControl();
        void EnableHighResolutionTimer();
        void DisableHighResolutionTimer();
        void StartRefreshLoop();
        void StopRefreshLoop();
        void UpdateOverlay();

        UINT TimerIntervalMs = 16; // default ~60fps

        HWND HostWnd   = nullptr;
        HWND MagWnd    = nullptr;
        HWND TargetWnd = nullptr;
        bool Visible     = false;
        bool Initialized = false;
        bool HighResolutionTimerEnabled = false;
        int ShowFrames   = 0;

        // Cached target rect to skip redundant SetWindowPos when CSP hasn't moved
        RECT LastRect = {};

        // Single-instance routing for C-style WndProc
        static MagnifierOverlay* Instance;
    };

}
