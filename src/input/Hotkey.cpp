#include "input/Hotkey.h"
#include <algorithm>
#include <cwctype>

namespace csp::input
{
    bool IsSupportedHotkey(const Hotkey& key)
    {
        if (key.Modifiers & ~(MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN)) return false;
        // F12 is reserved by the debugger; modifier-only bindings are not shortcuts.
        return (key.Key >= 'A' && key.Key <= 'Z') || (key.Key >= '0' && key.Key <= '9') ||
            (key.Key >= VK_F1 && key.Key <= VK_F24 && key.Key != VK_F12) ||
            key.Key == VK_SPACE || key.Key == VK_INSERT || key.Key == VK_DELETE ||
            key.Key == VK_HOME || key.Key == VK_END || key.Key == VK_PRIOR ||
            key.Key == VK_NEXT || key.Key == VK_PAUSE || (key.Key >= VK_LEFT && key.Key <= VK_DOWN);
    }
    std::wstring FormatHotkey(const Hotkey& key)
    {
        std::wstring text;
        if (key.Modifiers & MOD_CONTROL) text += L"Ctrl+";
        if (key.Modifiers & MOD_ALT) text += L"Alt+";
        if (key.Modifiers & MOD_SHIFT) text += L"Shift+";
        if (key.Modifiers & MOD_WIN) text += L"Win+";
        if ((key.Key >= 'A' && key.Key <= 'Z') || (key.Key >= '0' && key.Key <= '9'))
            return text + static_cast<wchar_t>(key.Key);
        if (key.Key >= VK_F1 && key.Key <= VK_F24) return text + L"F" + std::to_wstring(key.Key - VK_F1 + 1);
        const UINT scan = MapVirtualKeyW(key.Key, MAPVK_VK_TO_VSC_EX);
        LONG bits = static_cast<LONG>((scan & 0xff) << 16);
        if (scan & 0xff00) bits |= 1 << 24;
        wchar_t name[128]{};
        if (GetKeyNameTextW(bits, name, 128)) return text + name;
        return text + L"Unknown";
    }
    std::optional<Hotkey> ParseHotkey(const std::wstring& value)
    {
        std::wstring text = value;
        text.erase(std::remove_if(text.begin(), text.end(), [](wchar_t c) { return iswspace(c); }), text.end());
        std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
        Hotkey result{0, 0};
        size_t start = 0;
        for (size_t end; (end = text.find(L'+', start)) != std::wstring::npos; start = end + 1)
        {
            const auto token = text.substr(start, end - start);
            if (token == L"CTRL" || token == L"CONTROL") result.Modifiers |= MOD_CONTROL;
            else if (token == L"ALT") result.Modifiers |= MOD_ALT;
            else if (token == L"SHIFT") result.Modifiers |= MOD_SHIFT;
            else if (token == L"WIN" || token == L"META") result.Modifiers |= MOD_WIN;
            else return std::nullopt;
        }
        const auto token = text.substr(start);
        if (token.size() == 1) result.Key = token[0];
        else if (token.size() >= 2 && token.size() <= 3 && token[0] == L'F')
        {
            UINT number = 0;
            for (size_t i = 1; i < token.size(); ++i)
            {
                if (token[i] < L'0' || token[i] > L'9') return std::nullopt;
                number = number * 10 + token[i] - L'0';
            }
            if (number < 1 || number > 24) return std::nullopt;
            result.Key = VK_F1 + number - 1;
        }
        else if (token == L"SPACE") result.Key = VK_SPACE;
        else if (token == L"INSERT" || token == L"INS") result.Key = VK_INSERT;
        else if (token == L"DELETE" || token == L"DEL") result.Key = VK_DELETE;
        else if (token == L"HOME") result.Key = VK_HOME;
        else if (token == L"END") result.Key = VK_END;
        else if (token == L"PGUP") result.Key = VK_PRIOR;
        else if (token == L"PGDOWN") result.Key = VK_NEXT;
        else if (token == L"LEFT") result.Key = VK_LEFT;
        else if (token == L"RIGHT") result.Key = VK_RIGHT;
        else if (token == L"UP") result.Key = VK_UP;
        else if (token == L"DOWN") result.Key = VK_DOWN;
        else if (token == L"PAUSE") result.Key = VK_PAUSE;
        else return std::nullopt;
        return IsSupportedHotkey(result) ? std::optional(result) : std::nullopt;
    }
}
