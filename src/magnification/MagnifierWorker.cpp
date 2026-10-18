#include "magnification/MagnifierOverlay.h"
#include "util/Logger.h"
#include <algorithm>
#include <system_error>

namespace csp::magnification
{
    MagnifierOverlay::~MagnifierOverlay() { Shutdown(); }

    bool MagnifierOverlay::Initialize()
    {
        if (Worker.joinable()) return true;
        WakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!WakeEvent)
        {
            LOG_ERROR(L"Cannot create Magnification wake event (error %u)", GetLastError());
            return false;
        }
        Stopping.store(false);
        Failed.store(false);
        try { Worker = std::thread(&MagnifierOverlay::WorkerMain, this); }
        catch (const std::system_error& error)
        {
            LOG_ERROR(L"Cannot start Magnification worker (error %d)", error.code().value());
            CloseHandle(WakeEvent);
            WakeEvent = nullptr;
            return false;
        }
        return true;
    }

    void MagnifierOverlay::Shutdown()
    {
        Stopping.store(true);
        Presented.store(false);
        Wake();
        if (Worker.joinable()) Worker.join();
        if (WakeEvent) { CloseHandle(WakeEvent); WakeEvent = nullptr; }
        std::lock_guard lock(RequestMutex);
        Requested = {};
        Revision.store(0);
        WorkerRevision = 0;
    }

    void MagnifierOverlay::Wake() { if (WakeEvent) SetEvent(WakeEvent); }
    void MagnifierOverlay::Notify() { if (NotifyWindow) PostMessageW(NotifyWindow, NotifyMessage, 0, 0); }
    bool MagnifierOverlay::RequestIsCurrent() const
    { return !Stopping.load() && Revision.load() == WorkerRevision; }

    void MagnifierOverlay::ReportFailure()
    {
        // A late error from an obsolete capture must not fail a newer request.
        std::lock_guard lock(RequestMutex);
        if (!RequestIsCurrent()) return;
        Failed.store(true);
        Notify();
    }

    HWND MagnifierOverlay::TargetWindow() const
    {
        std::lock_guard lock(RequestMutex);
        return Requested.Target;
    }

    void MagnifierOverlay::Attach(HWND target)
    {
        std::lock_guard lock(RequestMutex);
        if (Requested.Target == target) return;
        Requested.Target = target;
        Requested.Revision = Revision.fetch_add(1) + 1;
        Wake();
    }
    void MagnifierOverlay::Detach()
    {
        std::lock_guard lock(RequestMutex);
        if (!Requested.Visible && !Requested.Target) return;
        Requested.Visible = false;
        Requested.Target = nullptr;
        Requested.Revision = Revision.fetch_add(1) + 1;
        Wake();
    }
    void MagnifierOverlay::Show()
    {
        std::lock_guard lock(RequestMutex);
        if (!Worker.joinable() || !Requested.Target || Requested.Visible) return;
        Requested.Visible = true;
        Requested.Resources = true;
        Failed.store(false);
        Requested.Revision = Revision.fetch_add(1) + 1;
        Wake();
    }
    void MagnifierOverlay::Hide()
    {
        std::lock_guard lock(RequestMutex);
        if (!Requested.Visible) return;
        Requested.Visible = false;
        Requested.Revision = Revision.fetch_add(1) + 1;
        Wake();
    }
    void MagnifierOverlay::ReleaseResources()
    {
        std::lock_guard lock(RequestMutex);
        if (!Requested.Resources) return;
        Requested.Visible = false;
        Requested.Resources = false;
        Requested.Revision = Revision.fetch_add(1) + 1;
        Wake();
    }
    void MagnifierOverlay::SetRefreshRate(int fps)
    {
        fps = std::clamp(fps, 1, 100);
        const UINT interval = static_cast<UINT>((1000 + fps - 1) / fps);
        std::lock_guard lock(RequestMutex);
        if (Requested.IntervalMs == interval) return;
        Requested.IntervalMs = interval;
        Requested.Revision = Revision.fetch_add(1) + 1;
        Wake();
    }

    void MagnifierOverlay::ApplyRequest()
    {
        Request request;
        {
            std::lock_guard lock(RequestMutex);
            request = Requested;
        }
        if (request.Revision == WorkerRevision) return;
        HideOnWorker();
        WorkerRevision = request.Revision;
        TargetWnd = request.Target;
        TimerIntervalMs = request.IntervalMs;
        if (!request.Resources) { DestroyResources(); return; }
        if (!request.Visible || !request.Target) return;
        if (!InitializeResources())
        {
            ReportFailure();
            return;
        }
        // Initialization can take hundreds of milliseconds. An intervening
        // off/target change must cancel its result before showing anything.
        if (RequestIsCurrent()) ShowOnWorker();
    }

    void MagnifierOverlay::WorkerMain()
    {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(com))
        {
            LOG_ERROR(L"Magnification worker COM initialization failed (0x%08X)", static_cast<unsigned>(com));
            Failed.store(true);
            Notify();
            return;
        }
        while (!Stopping.load())
        {
            // Handle latest UI intent before each queued timer/paint message.
            ApplyRequest();
            MSG msg{};
            if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT) break;
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            else if (MsgWaitForMultipleObjectsEx(1, &WakeEvent, INFINITE, QS_ALLINPUT,
                         MWMO_INPUTAVAILABLE) == WAIT_FAILED)
            {
                LOG_ERROR(L"Magnification worker wait failed (error %u)", GetLastError());
                Failed.store(true);
                Notify();
                break;
            }
        }
        DestroyResources();
        CoUninitialize();
    }
}
