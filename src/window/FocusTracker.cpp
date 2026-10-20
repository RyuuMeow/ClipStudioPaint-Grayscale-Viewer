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

        LocationHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
            nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
        MinimizeHook = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND,
            nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
        if (!LocationHook || !MinimizeHook)
        {
            LOG_ERROR(L"Cannot track window geometry/minimization (error %u)", GetLastError());
            Stop();
            return false;
        }

        LOG_INFO(L"FocusTracker started");
        return true;
    }

    void FocusTracker::Stop()
    {
        if (LocationHook) { UnhookWinEvent(LocationHook); LocationHook = nullptr; }
        if (MinimizeHook) { UnhookWinEvent(MinimizeHook); MinimizeHook = nullptr; }
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
        LONG objectId, LONG childId, DWORD, DWORD)
    {
        if (event == EVENT_OBJECT_LOCATIONCHANGE)
        {
            if (objectId != OBJID_WINDOW || childId != CHILDID_SELF) return;
            const HWND foreground = GetForegroundWindow();
            if (hwnd != foreground && hwnd != GetAncestor(foreground, GA_ROOTOWNER)) return;
            // Ignore our overlay and settings windows without opening their process.
            DWORD processId = 0;
            GetWindowThreadProcessId(hwnd, &processId);
            if (processId == GetCurrentProcessId()) return;
        }
        if (!FocusCallback)
        {
            return;
        }

        // Re-read foreground later: queued WinEvents may refer to an obsolete window.
        FocusCallback(hwnd, false);
    }

}
