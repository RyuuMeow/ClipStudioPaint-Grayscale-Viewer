#include "tray/TrayApplication.h"
#include <Windows.h>
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    auto directory = csp::core::SettingsDirectory();
    auto settings = directory.empty() ? std::filesystem::path{} : directory / L"native-settings.ini";
    bool smokeTest = false;
    bool configure = false;
    bool invalid = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::wstring arg(argv[i]);
        if (arg == L"--settings" && i + 1 < argc) settings = argv[++i];
        else if (arg == L"--smoke-test") smokeTest = true;
        else if (arg == L"--configure") configure = true;
        else if (arg != L"--minimized") invalid = true; // Legacy startup command remains valid.
    }
    LocalFree(argv);
    if (invalid || settings.empty())
    {
        MessageBoxW(nullptr, L"Usage: CSP-Grayscale-Viewer.exe [--configure] [--settings <path>]\nThe application runs in the Windows notification area.", L"CSP Grayscale Viewer", MB_OK | MB_ICONERROR);
        return 1;
    }
    std::error_code error;
    settings = std::filesystem::absolute(settings, error).lexically_normal();
    if (error) return 1;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 1;
    int result;
    {
        csp::tray::TrayApplication app;
        result = app.Run(settings, smokeTest, configure);
    }
    CoUninitialize();
    return result;
}
