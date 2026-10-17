#pragma once
#include "input/Hotkey.h"
#include <filesystem>
#include <string>
#include <vector>

namespace csp::core
{
    struct Settings
    {
        int RefreshRate = 60; // Zero selects the target monitor's refresh rate.
        input::Hotkey ToggleHotkey;
        bool FocusOnly = true;
        bool AutoStart = false;
        bool UseD3D = true;
        std::vector<std::wstring> TargetApps{L"CLIPStudioPaint.exe"};
        bool operator==(const Settings&) const = default;
    };
    std::wstring NormalizeProcessName(std::wstring Name);
    bool ParseSettings(const std::wstring& Text, Settings& Result, std::wstring& Error);
    std::wstring SerializeSettings(const Settings& Value);
    bool LoadSettings(const std::filesystem::path& Path, Settings& Result, std::wstring& Error);
    bool SaveSettings(const std::filesystem::path& Path, const Settings& Value, std::wstring& Error);
    std::filesystem::path SettingsDirectory();
}
