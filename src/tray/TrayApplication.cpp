#include "tray/TrayApplication.h"
#include "tray/SettingsDialogs.h"
#include "util/Logger.h"
#include "../../resources/resource.h"
#include <commctrl.h>
#include <windowsx.h>
#include <fstream>

namespace csp::tray
{
    namespace
    {
        constexpr wchar_t kWindowClass[] = L"CSP_GrayscaleNativeTray";
        constexpr UINT kTrayMessage = WM_APP + 1;
        constexpr UINT kShowMenu = WM_APP + 2;
        constexpr UINT_PTR kSmokeExit = 1;
        enum CommandId : UINT
        {
            Toggle = 100, Targets, Shortcut, FocusOnly, AutoStart, UseD3D, UseMag,
            Diagnostics, About, Exit, FpsBase = 200
        };
        constexpr int kRates[]{0, 15, 30, 60, 90, 120, 144};

        std::wstring ExecutablePath()
        {
            std::wstring path(32768, L'\0');
            const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (!size || size == path.size()) return {};
            path.resize(size);
            return path;
        }
        // Each settings profile has its own instance; explicit profiles make tests isolated.
        std::wstring ProfileId(const std::filesystem::path& path)
        {
            uint64_t hash = 14695981039346656037ULL;
            for (const auto c : path.wstring()) { hash ^= static_cast<wchar_t>(towlower(c)); hash *= 1099511628211ULL; }
            return std::to_wstring(hash);
        }
    }

    TrayApplication::~TrayApplication()
    {
        App.SetStateCallback({});
        IconUpdates.Shutdown();
        if (SessionStarted && !SaveDiagnostics())
            LOG_WARNING(L"Cannot save session diagnostics on exit");
        if (Window) DestroyWindow(Window);
        if (Icon) DestroyIcon(Icon);
        if (InstanceMutex) { ReleaseMutex(InstanceMutex); CloseHandle(InstanceMutex); }
    }

    int TrayApplication::Run(const std::filesystem::path& settingsPath, bool smokeTest, bool configure)
    {
        ConfigPath = settingsPath;
        const auto profile = ProfileId(ConfigPath);
        const auto title = L"CSP Grayscale Viewer " + profile;
        InstanceMutex = CreateMutexW(nullptr, TRUE, (L"Local\\CSP-Grayscale-Viewer-" + profile).c_str());
        if (!InstanceMutex) { ShowError(L"Cannot create the application instance lock."); return 1; }
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            if (HWND existing = FindWindowW(kWindowClass, title.c_str()))
                PostMessageW(existing, configure ? WM_COMMAND : kShowMenu, configure ? Targets : 0, 0);
            CloseHandle(InstanceMutex);
            InstanceMutex = nullptr;
            return 0;
        }
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&controls);
        WNDCLASSW wc{};
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpfnWndProc = WindowProc;
        wc.lpszClassName = kWindowClass;
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APP));
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;
        // Hidden top-level window receives Explorer restart, display and power broadcasts.
        Window = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, title.c_str(), WS_OVERLAPPED,
            0, 0, 0, 0, nullptr, nullptr, wc.hInstance, this);
        if (!Window) return 1;
        // Consume a launcher's SW_HIDE on the tray host, not on the first settings dialog.
        STARTUPINFOW startup{sizeof(startup)};
        GetStartupInfoW(&startup);
        if ((startup.dwFlags & STARTF_USESHOWWINDOW) && startup.wShowWindow == SW_HIDE)
            ShowWindow(Window, SW_HIDE);
        TaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        if (FAILED(LoadIconMetric(wc.hInstance, MAKEINTRESOURCEW(IDI_TRAY), LIM_SMALL, &Icon)))
        {
            ShowError(L"Cannot load the notification icon.");
            return 1;
        }

        std::wstring error;
        std::error_code ec;
        if (std::filesystem::exists(ConfigPath, ec))
        {
            if (!core::LoadSettings(ConfigPath, Config, error)) { ShowError(error); return 1; }
        }
        else if (ec) { ShowError(L"Cannot access the settings file."); return 1; }
        else if (ConfigPath.filename() == L"native-settings.ini")
        {
            const auto legacy = ConfigPath.parent_path() / L"settings.ini";
            if (std::filesystem::exists(legacy, ec))
            {
                if (!core::LoadSettings(legacy, Config, error)) { ShowError(error); return 1; }
                if (!core::SaveSettings(ConfigPath, Config, error)) { ShowError(error); return 1; }
                LOG_INFO(L"Imported legacy settings; original settings.ini preserved");
            }
        }
        if (!App.Initialize(Config)) { ShowError(L"Cannot initialize window tracking. See diagnostics or retry."); return 1; }
        SessionStarted = true;
        App.SetStateCallback([this](bool, bool) { UpdateIcon(); });
        if (!StartIcon()) { ShowError(L"Cannot start the notification icon worker."); return 1; }
        if (smokeTest) SetTimer(Window, kSmokeExit, 1500, nullptr);
        App.ForceNotifyState();
        if (configure && !smokeTest) PostMessageW(Window, WM_COMMAND, Targets, 0);
        return App.RunConsole();
    }

    bool TrayApplication::StartIcon()
    {
        NOTIFYICONDATAW IconData{};
        IconData.cbSize = sizeof(IconData);
        IconData.hWnd = Window;
        IconData.uID = 1;
        IconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        IconData.uCallbackMessage = kTrayMessage;
        IconData.hIcon = Icon;
        wcscpy_s(IconData.szTip, L"CSP Grayscale Viewer");
        return IconUpdates.Start(IconData);
    }
    void TrayApplication::UpdateIcon()
    {
        const auto status = L"CSP Grayscale Viewer - " + App.StatusText();
        IconUpdates.UpdateTooltip(status);
    }
    void TrayApplication::ShowMenu()
    {
        if (MenuOpen || DialogOpen) return;
        MenuOpen = true;
        const HWND previousForeground = GetForegroundWindow();
        HMENU menu = CreatePopupMenu();
        const auto toggleLabel = L"Grayscale preview\t" + input::FormatHotkey(Config.ToggleHotkey);
        AppendMenuW(menu, MF_STRING | (App.IsGrayscaleEnabled() ? MF_CHECKED : 0), Toggle, toggleLabel.c_str());
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, App.StatusText().c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, Targets, L"Target applications...");
        AppendMenuW(menu, MF_STRING, Shortcut, L"Set shortcut...");
        AppendMenuW(menu, MF_STRING | (Config.FocusOnly ? MF_CHECKED : 0), FocusOnly, L"Register shortcut only in target applications");
        HMENU rates = CreatePopupMenu();
        for (UINT i = 0; i < _countof(kRates); ++i)
        {
            const auto label = kRates[i] == 0 ? std::wstring(L"Automatic (monitor refresh rate)") : std::to_wstring(kRates[i]) + L" fps";
            AppendMenuW(rates, MF_STRING | (Config.RefreshRate == kRates[i] ? MF_CHECKED : 0), FpsBase + i, label.c_str());
        }
        AppendMenuW(rates, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(rates, MF_STRING | MF_GRAYED, 0, L"Magnification: up to 100 fps");
        const auto refreshLabel = Config.RefreshRate == 0 ? std::wstring(L"Refresh limit: Automatic") : L"Refresh limit: " + std::to_wstring(Config.RefreshRate) + L" fps";
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(rates), refreshLabel.c_str());
        HMENU renderers = CreatePopupMenu();
        AppendMenuW(renderers, MF_STRING | (Config.UseD3D ? MF_CHECKED : 0), UseD3D, L"Direct3D (automatic compatibility fallback)");
        AppendMenuW(renderers, MF_STRING | (!Config.UseD3D ? MF_CHECKED : 0), UseMag, L"Magnification (compatibility)");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(renderers), L"Renderer");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (Config.AutoStart ? MF_CHECKED : 0), AutoStart, L"Start with Windows");
        AppendMenuW(menu, MF_STRING, Diagnostics, L"Open diagnostics");
        AppendMenuW(menu, MF_STRING, About, L"About");
        AppendMenuW(menu, MF_STRING, Exit, L"Exit");
        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow(Window);
        const UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
            point.x, point.y, Window, nullptr);
        DestroyMenu(menu);
        PostMessageW(Window, WM_NULL, 0, 0);
        MenuOpen = false;
        if (command) Command(command);
        if (previousForeground && previousForeground != Window && IsWindow(previousForeground))
            SetForegroundWindow(previousForeground);
        App.ReevaluateFocus();
    }
    void TrayApplication::Command(UINT id)
    {
        switch (id)
        {
        case Toggle: App.ToggleGrayscale(); return;
        case Exit: App.Quit(); return;
        case Targets:
            DialogOpen = true;
            if (EditTargets(Window, Config.TargetApps)) { App.SetTargetApps(Config.TargetApps); Save(); }
            DialogOpen = false;
            return;
        case Shortcut:
            DialogOpen = true;
            if (EditHotkey(Window, Config.ToggleHotkey, [this](const input::Hotkey& key) { return App.SetHotkey(key.Modifiers, key.Key); })) Save();
            DialogOpen = false;
            return;
        case FocusOnly: Config.FocusOnly = !Config.FocusOnly; App.SetHotkeyFocusOnly(Config.FocusOnly); break;
        case UseD3D: case UseMag: Config.UseD3D = id == UseD3D; App.SetD3DRendererEnabled(Config.UseD3D); break;
        case AutoStart:
            if (!SetAutoStart(!Config.AutoStart)) return;
            Config.AutoStart = !Config.AutoStart;
            break;
        case Diagnostics: OpenDiagnostics(); return;
        case About:
            MessageBoxW(Window, L"CSP Grayscale Viewer 1.1\n\nNative Windows tray application.\nLeft-click: toggle grayscale. Right-click: settings.\nMagnification is retained as a compatibility renderer.\n\nMIT License", L"About", MB_OK | MB_ICONINFORMATION);
            return;
        default:
            if (id < FpsBase || id >= FpsBase + _countof(kRates)) return;
            Config.RefreshRate = kRates[id - FpsBase];
            App.SetRefreshRate(Config.RefreshRate);
            break;
        }
        Save();
        UpdateIcon();
    }
    bool TrayApplication::Save()
    {
        std::wstring error;
        if (core::SaveSettings(ConfigPath, Config, error)) return true;
        ShowError(error + L"\nChanges apply to this session but were not saved.");
        return false;
    }
    bool TrayApplication::SetAutoStart(bool enabled)
    {
        HKEY key = nullptr;
        LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (status == ERROR_SUCCESS)
        {
            if (enabled)
            {
                const auto command = L"\"" + ExecutablePath() + L"\" --settings \"" + ConfigPath.wstring() + L"\"";
                status = RegSetValueExW(key, L"CSP-Grayscale-Viewer", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                    static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
            }
            else
            {
                status = RegDeleteValueW(key, L"CSP-Grayscale-Viewer");
                if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
            }
            RegCloseKey(key);
        }
        if (status == ERROR_SUCCESS) return true;
        ShowError(L"Cannot update Windows startup settings (error " + std::to_wstring(status) + L").");
        return false;
    }
    bool TrayApplication::SaveDiagnostics()
    {
        const auto path = ConfigPath.parent_path() / L"diagnostics.log";
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return false;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const unsigned char bom[]{0xff, 0xfe};
        out.write(reinterpret_cast<const char*>(bom), sizeof(bom));
        // Keep identity outside the bounded history, even after a long session.
        const std::string build = __DATE__ " " __TIME__;
        const auto identity = L"Executable: " + ExecutablePath() +
            L"\r\nBuild: " + std::wstring(build.begin(), build.end()) +
            L"\r\nSettings: " + ConfigPath.wstring() + L"\r\n";
        out.write(reinterpret_cast<const char*>(identity.data()),
            static_cast<std::streamsize>(identity.size() * sizeof(wchar_t)));
        for (const auto& line : util::Logger::Instance().History())
            out.write(reinterpret_cast<const char*>(line.data()), static_cast<std::streamsize>(line.size() * sizeof(wchar_t)));
        out.close();
        return static_cast<bool>(out);
    }
    void TrayApplication::OpenDiagnostics()
    {
        if (!SaveDiagnostics()) { ShowError(L"Cannot write diagnostics."); return; }
        const auto path = ConfigPath.parent_path() / L"diagnostics.log";
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(Window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
            ShowError(L"Cannot open diagnostics. File: " + path.wstring());
    }
    void TrayApplication::ShowError(const std::wstring& message)
    {
        LOG_ERROR(L"%s", message.c_str());
        MessageBoxW(Window, message.c_str(), L"CSP Grayscale Viewer", MB_OK | MB_ICONERROR);
    }
    LRESULT CALLBACK TrayApplication::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        auto* self = reinterpret_cast<TrayApplication*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE)
        {
            self = static_cast<TrayApplication*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self)
        {
            if (self->TaskbarCreated && msg == self->TaskbarCreated)
            {
                self->IconUpdates.Recreate();
                return 0;
            }
            if (msg == kTrayMessage)
            {
                const auto event = LOWORD(lp);
                if (event == WM_CONTEXTMENU || event == NIN_KEYSELECT) self->ShowMenu();
                else if (event == NIN_SELECT && !self->DialogOpen) self->App.ToggleGrayscale();
                return 0;
            }
            if (msg == kShowMenu) { self->ShowMenu(); return 0; }
            if (msg == WM_COMMAND && !self->DialogOpen) { self->Command(LOWORD(wp)); return 0; }
            if (msg == WM_TIMER)
            {
                if (wp == kSmokeExit) self->App.Quit();
                return 0;
            }
            if (msg == WM_DISPLAYCHANGE) { self->App.DisplayConfigurationChanged(); return 0; }
            if (msg == WM_POWERBROADCAST)
            {
                if (wp == PBT_APMSUSPEND) self->App.SetSuspended(true);
                else if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) self->App.SetSuspended(false);
                return TRUE;
            }
            if (msg == WM_QUERYENDSESSION) return TRUE;
            if (msg == WM_ENDSESSION && wp) { self->App.Quit(); return 0; }
            if (msg == WM_CLOSE) { self->App.Quit(); return 0; }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}
