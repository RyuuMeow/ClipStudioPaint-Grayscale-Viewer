#include "input/HotkeyManager.h"
#include "input/Hotkey.h"
#include "util/Logger.h"

namespace csp::input
{
    HotkeyManager::~HotkeyManager()
    {
        UnregisterAll();
    }

    bool HotkeyManager::Register(int Id, UINT Modifiers, UINT Vk, std::function<void()> Action)
    {
        if (!RegisterHotKey(MessageWindow, Id, Modifiers | MOD_NOREPEAT, Vk))
        {
            LOG_ERROR(L"RegisterHotKey failed for id=%d vk=0x%X (error %u)", Id, Vk, GetLastError());
            return false;
        }

        Bindings.push_back({Id, Modifiers, Vk, std::move(Action)});

        LOG_INFO(L"Hotkey registered: %s (id=%d)", FormatHotkey({Modifiers, Vk}).c_str(), Id);
        return true;
    }

    void HotkeyManager::UnregisterAll()
    {
        for (auto& binding: Bindings)
        {
            UnregisterHotKey(MessageWindow, binding.Id);
        }
        if (!Bindings.empty())
        {
            LOG_INFO(L"Unregistered %zu hotkeys", Bindings.size());
        }
        Bindings.clear();
    }

    void HotkeyManager::Unregister(int Id)
    {
        for (auto it = Bindings.begin(); it != Bindings.end(); ++it)
        {
            if (it->Id == Id)
            {
                UnregisterHotKey(MessageWindow, Id);
                Bindings.erase(it);
                return;
            }
        }
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
