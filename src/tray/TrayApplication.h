#pragma once
#include "core/Application.h"
#include <shellapi.h>

namespace csp::tray
{
    class TrayApplication
    {
    public:
        ~TrayApplication();
        int Run(const std::filesystem::path& SettingsPath, bool SmokeTest = false, bool Configure = false);
    private:
        static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
        bool AddIcon();
        void UpdateIcon();
        void ShowMenu();
        void Command(UINT Id);
        bool Save();
        bool SetAutoStart(bool Enabled);
        void OpenDiagnostics();
        void ShowError(const std::wstring& Message);
        core::Application App;
        core::Settings Config;
        std::filesystem::path ConfigPath;
        HWND Window = nullptr;
        HANDLE InstanceMutex = nullptr;
        HICON Icon = nullptr;
        NOTIFYICONDATAW IconData{};
        UINT TaskbarCreated = 0;
        bool IconAdded = false;
        bool MenuOpen = false;
        bool DialogOpen = false;
    };
}
