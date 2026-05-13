#include "core/Application.h"
#include "util/Logger.h"

#include <Windows.h>


static DWORD GMainThreadId = 0;

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_CLOSE_EVENT)
    {
        PostThreadMessageW(GMainThreadId, WM_QUIT, 0, 0);
        return TRUE;
    }
    return FALSE;
}

// === Console mode entry point ===
#ifdef CSP_CONSOLE_MODE

int wmain(int, wchar_t* [])
{
    GMainThreadId = GetCurrentThreadId();
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    csp::core::Application App;

    if (!App.Initialize())
    {
        LOG_ERROR(L"Initialization failed. Exiting.");
        return 1;
    }

    return App.RunConsole();
}

// === GUI mode entry point (no console window) ===
#else

int WINAPI wWinMain(HINSTANCE, HINSTANCE,
                    LPWSTR, int)
{
    GMainThreadId = GetCurrentThreadId();

    if (AllocConsole())
    {
        FILE* fp = nullptr;
        _wfreopen_s(&fp, L"CONOUT$", L"w", stdout);
        _wfreopen_s(&fp, L"CONIN$", L"r", stdin);
        SetConsoleTitleW(L"CSP Grayscale Viewer");
    }

    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    csp::core::Application App;

    if (!App.Initialize())
    {
        LOG_ERROR(L"Initialization failed. Exiting.");
        MessageBoxW(nullptr, L"Failed to initialize. Check console for details.",
                    L"CSP Grayscale Viewer", MB_ICONERROR);
        return 1;
    }

    return App.RunConsole();
}

#endif
