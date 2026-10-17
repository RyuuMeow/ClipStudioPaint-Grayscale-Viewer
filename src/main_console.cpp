#include "core/Application.h"
#include "util/Logger.h"
#include <Windows.h>

namespace
{
    DWORD MainThreadId = 0;
    BOOL WINAPI ConsoleCtrlHandler(DWORD type)
    {
        if (type != CTRL_C_EVENT && type != CTRL_CLOSE_EVENT) return FALSE;
        PostThreadMessageW(MainThreadId, WM_QUIT, 0, 0);
        return TRUE;
    }
}

int wmain()
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 1;
    MainThreadId = GetCurrentThreadId();
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    int result = 1;
    {
        csp::core::Application app;
        if (app.Initialize()) result = app.RunConsole();
        else LOG_ERROR(L"Initialization failed. Exiting.");
    }
    SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
    CoUninitialize();
    return result;
}
