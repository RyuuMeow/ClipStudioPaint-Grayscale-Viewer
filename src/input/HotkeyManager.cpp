#include "input/HotkeyManager.h"
#include "util/Logger.h"

namespace csp::input
{
    HotkeyManager::~HotkeyManager()
    {
        UnregisterAll();
    }

    bool HotkeyManager::Register(int Id, UINT Modifiers, UINT Vk, std::function<void()> Action)
    {
        if (!RegisterHotKey(nullptr, Id, Modifiers, Vk))
        {
            LOG_ERROR(L"RegisterHotKey failed for id=%d vk=0x%X (error %u)", Id, Vk, GetLastError());
            return false;
        }

        Bindings.push_back({Id, Modifiers, Vk, std::move(Action)});

        std::wstring modStr;
        if (Modifiers & MOD_CONTROL)
        {
            modStr += L"Ctrl+";
        }
        if (Modifiers & MOD_ALT)
        {
            modStr += L"Alt+";
        }
        if (Modifiers & MOD_SHIFT)
        {
            modStr += L"Shift+";
        }

        wchar_t keyName[64] = {};
        UINT scanCode = MapVirtualKeyW(Vk, MAPVK_VK_TO_VSC);
        GetKeyNameTextW(static_cast<LONG>(scanCode << 16), keyName, _countof(keyName));

        LOG_INFO(L"Hotkey registered: %s%s (id=%d)", modStr.c_str(), keyName, Id);
        return true;
    }

    void HotkeyManager::UnregisterAll()
    {
        for (auto& binding: Bindings)
        {
            UnregisterHotKey(nullptr, binding.Id);
        }
        if (!Bindings.empty())
        {
            LOG_INFO(L"Unregistered %zu hotkeys", Bindings.size());
        }
        Bindings.clear();
    }

    void HotkeyManager::HandleHotkey(int Id)
    {
        for (auto& binding: Bindings)
        {
            if (binding.Id == Id)
            {
                if (binding.Action)
                {
                    binding.Action();
                }
                return;
            }
        }
        LOG_WARNING(L"Unknown hotkey id=%d", Id);
    }
}
