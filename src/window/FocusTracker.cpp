#include "window/FocusTracker.h"
#include "window/WindowFinder.h"
#include "util/Logger.h"

namespace csp::window
{
    FocusTracker::FocusChangeCallback FocusTracker::FocusCallback;

    FocusTracker::~FocusTracker()
    {
        Stop();
    }

    bool FocusTracker::Start(FocusChangeCallback callback)
    {
        if (Hook)
        {
            LOG_WARNING(L"FocusTracker already running");
            return true;
        }

        FocusCallback = std::move(callback);

        Hook = SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND,
            EVENT_SYSTEM_FOREGROUND,
            nullptr,
            WinEventProc,
            0,
            0,
            WINEVENT_OUTOFCONTEXT
        );

        if (!Hook)
        {
            LOG_ERROR(L"SetWinEventHook failed (error %u)", GetLastError());
            FocusCallback = nullptr;
            return false;
        }

        LOG_INFO(L"FocusTracker started");
        return true;
    }

    void FocusTracker::Stop()
    {
        if (Hook)
        {
            UnhookWinEvent(Hook);
            Hook = nullptr;
            FocusCallback = nullptr;
            LOG_INFO(L"FocusTracker stopped");
        }
    }

    void CALLBACK FocusTracker::WinEventProc(
        HWINEVENTHOOK, DWORD event, HWND hwnd,
        LONG, LONG, DWORD, DWORD)
    {
        if (event != EVENT_SYSTEM_FOREGROUND)
        {
            return;
        }
        if (!FocusCallback)
        {
            return;
        }

        const bool bIsCSP = IsCSPWindow(hwnd);
        FocusCallback(hwnd, bIsCSP);
    }

}
