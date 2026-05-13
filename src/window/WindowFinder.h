#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <mutex>

namespace csp::window
{

    struct WindowInfo
    {
        HWND Hwnd = nullptr;
        DWORD Pid = 0;
        std::wstring Title;
        std::wstring ClassName;
    };

    class WindowFinder
    {
    public:
        static void SetTargetProcessNames(const std::vector<std::wstring>& Names);

        static std::vector<std::wstring> GetTargetProcessNames();

        static bool IsTargetWindow(HWND Hwnd);

        static HWND FindTargetWindow();

        static std::wstring GetProcessName(DWORD Pid);

    private:
        static std::mutex Mutex;
        static std::vector<std::wstring> TargetNames;
    };

    inline bool IsCSPWindow(HWND Hwnd)
    { return WindowFinder::IsTargetWindow(Hwnd); }
    inline HWND FindMainCSPWindow()
    { return WindowFinder::FindTargetWindow(); }

}
