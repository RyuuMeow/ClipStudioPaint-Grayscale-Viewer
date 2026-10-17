#include "core/Settings.h"
#include "core/Application.h"
#include "d3d/RenderPolicy.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
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
        const HWND control = FindWindowExW(HWND_MESSAGE, nullptr, L"CSP_GrayscaleControl", nullptr);
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
        std::cout << "All core regression tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
