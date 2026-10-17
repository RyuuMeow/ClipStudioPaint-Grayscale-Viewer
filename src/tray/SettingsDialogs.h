#pragma once
#include "input/Hotkey.h"
#include <functional>
#include <vector>

namespace csp::tray
{
    bool EditHotkey(HWND Owner, input::Hotkey& Key, const std::function<bool(const input::Hotkey&)>& Apply);
    bool EditTargets(HWND Owner, std::vector<std::wstring>& Targets);
}
