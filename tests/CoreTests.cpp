#include "core/Settings.h"
#include "core/Application.h"
#include "d3d/RenderPolicy.h"
#include "window/WindowGeometry.h"
#include "tray/NotificationIcon.h"
#include <Windows.h>
#include <fstream>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    HWND OwnControlWindow()
    {
        HWND window = nullptr;
        while ((window = FindWindowExW(HWND_MESSAGE, window, L"CSP_GrayscaleControl", nullptr)))
        {
            DWORD process = 0;
            GetWindowThreadProcessId(window, &process);
            if (process == GetCurrentProcessId()) return window;
        }
        return nullptr;
    }
    void SettingsRoundTrip()
    {
        csp::core::Settings original;
        original.RefreshRate = 120;
        original.TargetApps = {L"CLIPStudioPaint.exe", L"繪圖程式.exe", L"drawing, test.exe"};
        original.ToggleHotkey = {MOD_CONTROL | MOD_ALT | MOD_WIN, VK_F10};
        original.FocusOnly = false;
        original.AutoStart = true;
        original.UseD3D = true;
        csp::core::Settings loaded;
        std::wstring error;
        const auto encoded = csp::core::SerializeSettings(original);
        Check(csp::core::ParseSettings(encoded, loaded, error), "settings parse");
        Check(loaded == original, "120 fps load must preserve targets, shortcut, startup and renderer");
        Check(csp::core::SerializeSettings(loaded) == encoded, "loading must not rewrite values");
        original.TargetApps.clear();
        original.RefreshRate = 0;
        Check(csp::core::ParseSettings(csp::core::SerializeSettings(original), loaded, error), "empty target list parse");
        Check(loaded == original, "empty targets and automatic refresh must survive restart");
    }
    void LegacyMigration()
    {
        const std::wstring text = L"[%General]\nRefreshRate=144\nTargetApps=CLIPStudioPaint.exe, \"paint, test.exe\", \\x7e6a\\x5716.exe\n"
            L"[Hotkey]\nFocusOnly=false\nSequence=Ctrl+Alt+F9\n[System]\nAutoStart=true\nStartMinimized=true\n[Renderer]\nUseD3D=true\n";
        csp::core::Settings settings;
        std::wstring error;
        Check(csp::core::ParseSettings(text, settings, error), "legacy Qt INI import");
        Check(settings.TargetApps == std::vector<std::wstring>{L"CLIPStudioPaint.exe", L"paint, test.exe", L"繪圖.exe"}, "Qt list/quotes/hex decoding");
        Check(settings.RefreshRate == 144 && settings.AutoStart && settings.UseD3D && !settings.FocusOnly, "legacy options preserved");
        Check(settings.ToggleHotkey == csp::input::Hotkey{MOD_CONTROL | MOD_ALT, VK_F9}, "legacy shortcut migration");
        Check(csp::core::ParseSettings(L"[%General]\nTargetApps=@Invalid()\n", settings, error), "legacy empty list");
        Check(settings.TargetApps.empty(), "legacy empty list remains empty");
    }
    void InvalidSettingsDoNotPartiallyApply()
    {
        csp::core::Settings settings;
        settings.RefreshRate = 30;
        settings.TargetApps = {L"kept.exe"};
        const auto before = settings;
        std::wstring error;
        for (const auto* invalid : {
            L"[General]\nRefreshRate=9999\n", L"[General]\nRefreshRate=60oops\n",
            L"[App]\nVersion=99\n", L"[Hotkey]\nFocusOnly=perhaps\n",
            L"[General]\nTargetApps=C:\\paint.exe\n", L"[General]\nTargetApps=\"unfinished.exe\n"})
        {
            Check(!csp::core::ParseSettings(invalid, settings, error), "invalid settings rejected");
            Check(settings == before && !error.empty(), "failed load leaves current settings intact");
        }
    }
    void FilesAreSavedAndLoadedAtomically()
    {
        const auto directory = std::filesystem::temp_directory_path() / (L"csp-settings-test-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directory(directory);
        const auto path = directory / L"設定.ini";
        std::wstring error;
        csp::core::Settings settings;
        settings.TargetApps = {L"繪圖.exe"};
        Check(csp::core::SaveSettings(path, settings, error), "save UTF-8 settings to Unicode path");
        csp::core::Settings loaded;
        Check(csp::core::LoadSettings(path, loaded, error) && loaded == settings, "load actual saved file");
        settings.RefreshRate = 120;
        Check(csp::core::SaveSettings(path, settings, error), "atomic replacement");
        Check(csp::core::LoadSettings(path, loaded, error) && loaded == settings, "replacement survives reload");
        const auto valid = settings;
        settings.RefreshRate = -1;
        Check(!csp::core::SaveSettings(path, settings, error), "invalid settings cannot overwrite file");
        Check(csp::core::LoadSettings(path, loaded, error) && loaded == valid, "failed save retains original file");
        Check(!std::filesystem::exists(path.wstring() + L".tmp"), "no temporary file after successful replace");
        std::filesystem::remove(path);
        std::filesystem::remove(directory);
    }
    void Hotkeys()
    {
        auto parsed = csp::input::ParseHotkey(L"Ctrl+Alt+Win+F9");
        Check(parsed == csp::input::Hotkey{MOD_CONTROL | MOD_ALT | MOD_WIN, VK_F9}, "all shortcut modifiers retained");
        Check(csp::input::FormatHotkey(*parsed) == L"Ctrl+Alt+Win+F9", "shortcut display matches registration");
        Check(!csp::input::ParseHotkey(L"F12") && !csp::input::ParseHotkey(L"F25") &&
            !csp::input::ParseHotkey(L"Unknown+A") && !csp::input::ParseHotkey(L"Ctrl+"), "reject reserved or unsupported shortcuts");
        Check(!csp::input::IsSupportedHotkey({MOD_CONTROL, VK_CONTROL}), "modifier alone is not a shortcut");
    }
    void GeometryAndPacing()
    {
        using csp::d3d::CanCaptureWindow;
        const RECT monitor{-1920, 0, 0, 1080};
        Check(CanCaptureWindow({-1900, 10, -20, 1000}, monitor, DMDO_DEFAULT), "negative monitor coordinates supported");
        Check(!CanCaptureWindow({-500, 10, 500, 1000}, monitor, DMDO_DEFAULT), "spanning window requires compatibility fallback");
        Check(!CanCaptureWindow({-1900, 10, -20, 1000}, monitor, DMDO_90), "portrait requires compatibility fallback");
        Check(!CanCaptureWindow({-1900, 10, -1900, 1000}, monitor, DMDO_DEFAULT), "empty window rejected");
        using csp::window::ClipMaximizedFrame;
        const RECT ultrawide{0, 0, 3440, 1440};
        const RECT maximized{-8, -8, 3448, 1448};
        const RECT clipped = ClipMaximizedFrame(maximized, ultrawide, true, {8, 8});
        Check(EqualRect(&clipped, &ultrawide), "reported maximized eight-pixel border is excluded");
        Check(CanCaptureWindow(clipped, ultrawide, DMDO_DEFAULT), "maximized target stays on Direct3D");
        Check(!CanCaptureWindow(ClipMaximizedFrame(maximized, ultrawide, false, {8, 8}), ultrawide, DMDO_DEFAULT),
            "restored spanning target is not cropped");
        Check(!CanCaptureWindow(ClipMaximizedFrame({-8, -8, 4000, 1448}, ultrawide, true, {8, 8}), ultrawide, DMDO_DEFAULT),
            "maximized target extending beyond sizing border still falls back");
        const RECT scaled = ClipMaximizedFrame({-1936, -16, 16, 1096}, monitor, true, {16, 16});
        Check(EqualRect(&scaled, &monitor), "scaled border and negative monitor coordinates are supported");
        Check(!CanCaptureWindow(clipped, ultrawide, DMDO_90), "border normalization does not enable rotated capture");
        using csp::d3d::EffectiveRefreshRate;
        Check(EffectiveRefreshRate(144, 60, false) == 60, "do not exceed monitor refresh");
        Check(EffectiveRefreshRate(0, 144, false) == 144, "automatic rate follows monitor");
        Check(EffectiveRefreshRate(144, 144, true) == 100, "Magnification respects SetTimer limit");
        Check(EffectiveRefreshRate(0, 0, false) == 60, "unknown monitor rate has safe fallback");
    }
    void NativeHotkeyRoutingAndConflict()
    {
        csp::core::Settings settings;
        settings.TargetApps.clear();
        settings.FocusOnly = false;
        settings.ToggleHotkey = {MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F22};
        csp::core::Application app;
        Check(app.Initialize(settings) && app.IsHotkeyRegistered(), "native shortcut registered");
        const HWND control = OwnControlWindow();
        Check(control != nullptr, "native shortcut has a receiving HWND");
        Check(!app.IsGrayscaleEnabled(), "preview starts disabled");
        SendMessageW(control, WM_HOTKEY, 1, 0);
        Check(app.IsGrayscaleEnabled(), "WM_HOTKEY dispatches through native window procedure");
        const HWND blocker = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
            nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(blocker != nullptr, "create shortcut conflict window");
        const bool reserved = RegisterHotKey(blocker, 9, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F23) != FALSE;
        const bool rejected = reserved && !app.SetHotkey(MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F23);
        UnregisterHotKey(blocker, 9);
        DestroyWindow(blocker);
        Check(reserved && rejected, "conflicting replacement is rejected");
        SendMessageW(control, WM_HOTKEY, 1, 0);
        Check(!app.IsGrayscaleEnabled() && app.IsHotkeyRegistered(), "conflict retains working shortcut");
        app.SetSuspended(true);
        Check(!app.IsHotkeyRegistered(), "suspension releases shortcut");
        app.SetSuspended(false);
        Check(app.IsHotkeyRegistered(), "resume restores shortcut");
        app.SetHotkeyFocusOnly(true);
        Check(!app.IsHotkeyRegistered(), "focus-only shortcut released outside target");
        SendMessageW(control, WM_HOTKEY, 1, 0);
        Check(!app.IsGrayscaleEnabled(), "queued hotkey ignored after unregistration");
    }

    void SlowNotificationIconDoesNotBlockRequests()
    {
        struct Call { DWORD Action; std::wstring Tip; DWORD Thread; };
        struct Gate
        {
            std::mutex Mutex;
            std::condition_variable Wake;
            bool Entered = false, Released = false;
            std::vector<Call> Calls;
        } gate;
        csp::tray::NotificationIcon icon([&](DWORD action, PNOTIFYICONDATAW data) -> BOOL
        {
            std::unique_lock lock(gate.Mutex);
            gate.Calls.push_back({action, data->szTip, GetCurrentThreadId()});
            if (action == NIM_MODIFY && std::wstring(data->szTip) == L"hold")
            {
                gate.Entered = true;
                gate.Wake.notify_all();
                gate.Wake.wait(lock, [&] { return gate.Released; });
            }
            return TRUE;
        });
        // Unblock the fake Shell call before the worker is joined, including on failure.
        struct Release
        {
            Gate& Value;
            void Now()
            {
                std::lock_guard lock(Value.Mutex);
                Value.Released = true;
                Value.Wake.notify_all();
            }
            ~Release() { Now(); }
        } release{gate};
        const auto settled = [&]
        {
            const ULONGLONG deadline = GetTickCount64() + 2000;
            while (!icon.IsSynchronized() && GetTickCount64() < deadline) Sleep(1);
            return icon.IsSynchronized();
        };
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        wcscpy_s(data.szTip, L"initial");
        Check(icon.Start(data) && settled(), "notification worker registers with injected Shell transport");
        icon.UpdateTooltip(L"hold");
        {
            std::unique_lock lock(gate.Mutex);
            Check(gate.Wake.wait_for(lock, std::chrono::seconds(2), [&] { return gate.Entered; }),
                "notification transport is deliberately blocked");
        }
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 100; ++i) icon.UpdateTooltip(L"intermediate " + std::to_wstring(i));
        icon.UpdateTooltip(L"final");
        Check(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(50),
            "tooltip requests do not wait for a blocked Shell call");
        Check(!icon.IsSynchronized(), "blocked icon has not yet applied the latest request");
        release.Now();
        Check(settled(), "icon applies the latest tooltip after Shell unblocks");
        icon.Recreate();
        Check(settled(), "Explorer recreation registers the latest icon state");
        icon.Shutdown();
        unsigned adds = 0, deletes = 0, versions = 0, modifies = 0;
        for (const auto& call : gate.Calls)
        {
            Check(call.Thread != GetCurrentThreadId(), "all Shell operations run outside the UI thread");
            Check(call.Tip.find(L"intermediate") == std::wstring::npos, "obsolete tooltip requests are coalesced");
            adds += call.Action == NIM_ADD;
            deletes += call.Action == NIM_DELETE;
            versions += call.Action == NIM_SETVERSION;
            modifies += call.Action == NIM_MODIFY;
        }
        Check(adds == 2 && deletes == 2 && versions == 2 && modifies == 2,
            "register, version, coalesced update, recreation and shutdown stay ordered");
        Check(!icon.IsRegistered(), "shutdown removes the notification icon");
    }

    void FocusRecoveryWithoutNotifications()
    {
        struct TestWindow
        {
            HWND Handle = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"",
                WS_POPUP | WS_VISIBLE, -30000, -30000, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            ~TestWindow() { if (Handle) DestroyWindow(Handle); }
        } target, blocker;
        Check(target.Handle && blocker.Handle, "create isolated focus test windows");
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, _countof(executable));
        csp::core::Settings settings;
        settings.TargetApps = {std::filesystem::path(executable).filename().wstring()};
        settings.FocusOnly = true;
        settings.ToggleHotkey = {MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F21};
        HWND foreground = nullptr;
        csp::core::Application app([&] { return foreground; });
        Check(app.Initialize(settings) && !app.IsHotkeyRegistered(), "no shortcut without target focus");
        const HWND control = OwnControlWindow();
        Check(control != nullptr, "find own focus recovery control window");
        // Dispatch only timers: deliberately withhold foreground/location notifications.
        // The foreground provider never changes the user's actual desktop focus.
        const auto waitForRegistration = [&](bool expected, DWORD timeout = 1500)
        {
            const auto deadline = GetTickCount64() + timeout;
            do
            {
                MSG message{};
                while (PeekMessageW(&message, control, WM_TIMER, WM_TIMER, PM_REMOVE))
                    DispatchMessageW(&message);
                if (app.IsHotkeyRegistered() == expected) return true;
                Sleep(5);
            } while (GetTickCount64() < deadline);
            return false;
        };
        foreground = target.Handle;
        Check(waitForRegistration(true), "missed focus notification must not leave the shortcut unregistered");
        foreground = nullptr;
        Check(waitForRegistration(false), "missed focus loss releases the target-only shortcut");
        app.ReevaluateFocus(); // An activation transition can temporarily report no foreground.
        foreground = target.Handle;
        Check(waitForRegistration(true), "transient null foreground recovers without another notification");
        ShowWindow(target.Handle, SW_HIDE);
        Check(waitForRegistration(false), "hidden target releases the shortcut");
        ShowWindow(target.Handle, SW_SHOWNOACTIVATE);
        Check(waitForRegistration(true), "same foreground handle becoming visible restores the shortcut");
        ShowWindow(target.Handle, SW_SHOWMINNOACTIVE);
        Check(waitForRegistration(false), "minimized target releases the shortcut");
        ShowWindow(target.Handle, SW_SHOWNOACTIVATE);
        Check(waitForRegistration(true), "same foreground handle restored from minimization recovers the shortcut");
        app.SetSuspended(true);
        Check(!waitForRegistration(true, 350), "focus watchdog must respect suspension");
        app.SetSuspended(false);
        Check(app.IsHotkeyRegistered(), "resume restores target-only registration");

        foreground = nullptr;
        app.ReevaluateFocus();
        Check(RegisterHotKey(blocker.Handle, 9, settings.ToggleHotkey.Modifiers, settings.ToggleHotkey.Key) != FALSE,
            "reserve focus test shortcut temporarily");
        foreground = target.Handle;
        app.ReevaluateFocus();
        const bool conflictDetected = !app.IsHotkeyRegistered();
        UnregisterHotKey(blocker.Handle, 9);
        Check(conflictDetected, "focus activation records shortcut conflict");
        Check(waitForRegistration(true), "released shortcut conflict recovers without switching focus again");
    }
}

int main()
{
    try
    {
        SettingsRoundTrip();
        LegacyMigration();
        InvalidSettingsDoNotPartiallyApply();
        FilesAreSavedAndLoadedAtomically();
        Hotkeys();
        GeometryAndPacing();
        NativeHotkeyRoutingAndConflict();
        FocusRecoveryWithoutNotifications();
        SlowNotificationIconDoesNotBlockRequests();
        std::cout << "All core regression tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
