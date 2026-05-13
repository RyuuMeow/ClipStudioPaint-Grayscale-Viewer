#pragma once

#include <Windows.h>
#include <functional>

namespace csp::window
{
    class FocusTracker
    {
    public:
        using FocusChangeCallback = std::function<void(HWND NewForeground, bool IsCSP)>;

        FocusTracker() = default;
        ~FocusTracker();

        FocusTracker(const FocusTracker&) = delete;
        FocusTracker& operator=(const FocusTracker&) = delete;

        bool Start(FocusChangeCallback callback);
        void Stop();

        bool IsRunning() const
        { return Hook != nullptr; }

    private:
        static void CALLBACK WinEventProc(
            HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
            LONG idObject, LONG idChild, DWORD idEventThread, DWORD dwmsEventTime
        );

        HWINEVENTHOOK Hook = nullptr;

        static FocusChangeCallback FocusCallback;
    };

}
