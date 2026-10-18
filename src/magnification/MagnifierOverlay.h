#pragma once
#include <Windows.h>
#include <magnification.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

namespace csp::magnification
{
    class MagnifierOverlay
    {
    public:
        MagnifierOverlay() = default;
        ~MagnifierOverlay();
        MagnifierOverlay(const MagnifierOverlay&) = delete;
        MagnifierOverlay& operator=(const MagnifierOverlay&) = delete;

        // Start the worker; API/window creation happens asynchronously on Show.
        bool Initialize();
        void Shutdown();
        // Release idle API/window resources without waiting on the UI thread.
        void ReleaseResources();
        void Attach(HWND targetWindow);
        void Detach();
        void Show();
        void Hide();
        void SetRefreshRate(int fps);
        bool IsVisible() const { return Presented.load() && PresentedRevision.load() == Revision.load(); }
        bool HasFailed() const { return Failed.load(); }
        bool HasResources() const { return ResourcesReady.load(); }
        void SetNotificationWindow(HWND window, UINT message) { NotifyWindow = window; NotifyMessage = message; }
        bool IsInitialized() const { return Worker.joinable(); }
        HWND TargetWindow() const;

    private:
        struct Request
        {
            HWND Target = nullptr;
            bool Visible = false;
            bool Resources = false;
            UINT IntervalMs = 17;
            uint64_t Revision = 0;
        };
        void Wake();
        void WorkerMain();
        void ApplyRequest();
        bool RequestIsCurrent() const;
        void Notify();
        void ReportFailure();
        bool InitializeResources();
        void DestroyResources();
        void ShowOnWorker();
        void HideOnWorker();
        static LRESULT CALLBACK HostWndProc(HWND, UINT, WPARAM, LPARAM);
        bool CreateHostWindow();
        bool CreateMagnifierControl();
        void EnableHighResolutionTimer();
        void DisableHighResolutionTimer();
        void StartRefreshLoop();
        void StopRefreshLoop();
        void UpdateOverlay();

        // Only these fields are shared with the UI thread. No lock is held
        // while calling Magnification, painting, or destroying its windows.
        mutable std::mutex RequestMutex;
        Request Requested;
        std::atomic<uint64_t> Revision{0};
        std::atomic<bool> Stopping{false};
        std::atomic<bool> Presented{false};
        std::atomic<uint64_t> PresentedRevision{0};
        std::atomic<bool> Failed{false};
        std::atomic<bool> ResourcesReady{false};
        HANDLE WakeEvent = nullptr;
        std::thread Worker;
        HWND NotifyWindow = nullptr;
        UINT NotifyMessage = 0;

        // All Magnification/COM/window state belongs to the worker.
        uint64_t WorkerRevision = 0;
        UINT TimerIntervalMs = 17;
        HWND HostWnd = nullptr;
        HWND MagWnd = nullptr;
        HWND TargetWnd = nullptr;
        bool Visible = false;
        bool Initialized = false;
        bool HighResolutionTimerEnabled = false;
        int ShowFrames = 0;
        RECT LastRect{};
    };
}
