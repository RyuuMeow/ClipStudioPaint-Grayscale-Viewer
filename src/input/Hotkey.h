#pragma once
#include <Windows.h>
#include <optional>
#include <string>

namespace csp::input
{
    struct Hotkey
    {
        UINT Modifiers = 0;
        UINT Key = VK_F9;
        bool operator==(const Hotkey&) const = default;
    };
    bool IsSupportedHotkey(const Hotkey& Key);
    std::wstring FormatHotkey(const Hotkey& Key);
    std::optional<Hotkey> ParseHotkey(const std::wstring& Text);
}
