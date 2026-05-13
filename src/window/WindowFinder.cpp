#include "window/WindowFinder.h"
#include "util/Logger.h"

#include <algorithm>
#include <Psapi.h>

#pragma comment(lib, "Psapi.lib")

namespace csp::window
{
    std::mutex WindowFinder::Mutex;
    std::vector<std::wstring> WindowFinder::TargetNames = { L"CLIPStudioPaint.exe" };

    void WindowFinder::SetTargetProcessNames(const std::vector<std::wstring>& Names)
    {
        std::lock_guard Lock(Mutex);
        TargetNames = Names;
        LOG_INFO(L"Target apps updated (%zu entries)", Names.size());
    }

    std::vector<std::wstring> WindowFinder::GetTargetProcessNames()
    {
        std::lock_guard Lock(Mutex);
        return TargetNames;
    }

    bool WindowFinder::IsTargetWindow(const HWND Hwnd)
    {
        if (!Hwnd || !IsWindow(Hwnd))
        {
            return false;
        }

        DWORD pid = 0;
        GetWindowThreadProcessId(Hwnd, &pid);
        if (pid == 0)
        {
            return false;
        }

        const std::wstring ProcName = GetProcessName(pid);
        if (ProcName.empty())
        {
            return false;
        }

        std::lock_guard lock(Mutex);
        for (const auto& target : TargetNames)
        {
            if (_wcsicmp(ProcName.c_str(), target.c_str()) == 0)
            {
                return true;
            }
        }
        return false;
    }

    HWND WindowFinder::FindTargetWindow()
    {
        struct FindCtx
        {
            HWND Result = nullptr;
            WindowFinder* Finder = nullptr;
        } ctx;

        EnumWindows([](const HWND Hwnd, const LPARAM LParam) -> BOOL {
            if (!IsWindowVisible(Hwnd))
            {
                return TRUE;
            }

            wchar_t title[512] = {};
            GetWindowTextW(Hwnd, title, _countof(title));
            if (wcslen(title) == 0)
            {
                return TRUE;
            }

            if (IsTargetWindow(Hwnd))
            {
                auto* C = reinterpret_cast<FindCtx*>(LParam);
                C->Result = Hwnd;

                DWORD Pid = 0;
                GetWindowThreadProcessId(Hwnd, &Pid);
                LOG_INFO(L"Found target window: pid=%u \"%s\"", Pid, title);
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));

        return ctx.Result;
    }

    std::wstring WindowFinder::GetProcessName(DWORD Pid)
    {
        const HANDLE HProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Pid);
        if (!HProc)
        {
            return {};
        }

        wchar_t Path[MAX_PATH] = {};
        DWORD Size = MAX_PATH;
        if (QueryFullProcessImageNameW(HProc, 0, Path, &Size))
        {
            CloseHandle(HProc);
            std::wstring fullPath(Path);
            auto pos = fullPath.find_last_of(L"\\/");
            return (pos != std::wstring::npos) ? fullPath.substr(pos + 1) : fullPath;
        }

        CloseHandle(HProc);
        return {};
    }

}
