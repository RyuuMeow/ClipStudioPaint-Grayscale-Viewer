#pragma once

#include <Windows.h>
#include <functional>
#include <vector>

namespace csp::input
{
    class HotkeyManager
    {
    public:
        struct HotkeyBinding
        {
            int Id;
            UINT Modifiers;
            UINT Vk;
            std::function<void()> Action;
        };

        HotkeyManager() = default;
        ~HotkeyManager();

        HotkeyManager(const HotkeyManager&) = delete;
        HotkeyManager& operator=(const HotkeyManager&) = delete;

        bool Register(int Id, UINT Modifiers, UINT Vk, std::function<void()> Action);
        void UnregisterAll();

        void HandleHotkey(int Id);

    private:
        std::vector<HotkeyBinding> Bindings;
    };

}
