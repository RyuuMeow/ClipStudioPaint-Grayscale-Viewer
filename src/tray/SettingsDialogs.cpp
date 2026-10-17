#include "tray/SettingsDialogs.h"
#include "core/Settings.h"
#include "window/WindowFinder.h"
#include "../../resources/resource.h"
#include <commctrl.h>
#include <algorithm>
#include <set>
#include <sstream>

namespace csp::tray
{
    namespace
    {
        std::wstring ControlText(HWND dialog, int id)
        {
            HWND control = GetDlgItem(dialog, id);
            const int length = GetWindowTextLengthW(control);
            std::wstring text(static_cast<size_t>(length) + 1, L'\0');
            GetWindowTextW(control, text.data(), length + 1);
            text.resize(length);
            return text;
        }
        void CenterDialog(HWND dialog)
        {
            POINT cursor{};
            GetCursorPos(&cursor);
            MONITORINFO info{sizeof(info)};
            GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &info);
            RECT rect{};
            GetWindowRect(dialog, &rect);
            SetWindowPos(dialog, HWND_TOP, info.rcWork.left + (info.rcWork.right - info.rcWork.left - rect.right + rect.left) / 2,
                info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - rect.bottom + rect.top) / 2,
                0, 0, SWP_NOSIZE);
        }
        struct HotkeyContext
        {
            input::Hotkey Value;
            const std::function<bool(const input::Hotkey&)>& Apply;
        };
        INT_PTR CALLBACK HotkeyProc(HWND dialog, UINT msg, WPARAM wp, LPARAM lp)
        {
            auto* context = reinterpret_cast<HotkeyContext*>(GetWindowLongPtrW(dialog, DWLP_USER));
            if (msg == WM_INITDIALOG)
            {
                context = reinterpret_cast<HotkeyContext*>(lp);
                SetWindowLongPtrW(dialog, DWLP_USER, lp);
                WORD flags = 0;
                if (context->Value.Modifiers & MOD_CONTROL) flags |= HOTKEYF_CONTROL;
                if (context->Value.Modifiers & MOD_ALT) flags |= HOTKEYF_ALT;
                if (context->Value.Modifiers & MOD_SHIFT) flags |= HOTKEYF_SHIFT;
                if (MapVirtualKeyW(context->Value.Key, MAPVK_VK_TO_VSC_EX) & 0xff00) flags |= HOTKEYF_EXT;
                SendDlgItemMessageW(dialog, IDC_HOTKEY, HKM_SETRULES, 0, 0);
                SendDlgItemMessageW(dialog, IDC_HOTKEY, HKM_SETHOTKEY, MAKEWORD(context->Value.Key, flags), 0);
                CheckDlgButton(dialog, IDC_WIN_KEY, context->Value.Modifiers & MOD_WIN ? BST_CHECKED : BST_UNCHECKED);
                CenterDialog(dialog);
                return TRUE;
            }
            if (msg == WM_COMMAND && context)
            {
                if (LOWORD(wp) == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
                if (LOWORD(wp) == IDOK)
                {
                    const WORD raw = static_cast<WORD>(SendDlgItemMessageW(dialog, IDC_HOTKEY, HKM_GETHOTKEY, 0, 0));
                    input::Hotkey proposed{0, LOBYTE(raw)};
                    const BYTE flags = HIBYTE(raw);
                    if (flags & HOTKEYF_CONTROL) proposed.Modifiers |= MOD_CONTROL;
                    if (flags & HOTKEYF_ALT) proposed.Modifiers |= MOD_ALT;
                    if (flags & HOTKEYF_SHIFT) proposed.Modifiers |= MOD_SHIFT;
                    if (IsDlgButtonChecked(dialog, IDC_WIN_KEY) == BST_CHECKED) proposed.Modifiers |= MOD_WIN;
                    if (!input::IsSupportedHotkey(proposed))
                    {
                        MessageBoxW(dialog, L"Use a letter, number, function key (except F12), Space, or navigation key.", L"Unsupported shortcut", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    if (!context->Apply(proposed))
                    {
                        MessageBoxW(dialog, L"This shortcut is reserved or used by another application. Your previous shortcut is unchanged.", L"Shortcut unavailable", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    context->Value = proposed;
                    EndDialog(dialog, IDOK);
                    return TRUE;
                }
            }
            return FALSE;
        }
        INT_PTR CALLBACK TargetsProc(HWND dialog, UINT msg, WPARAM wp, LPARAM lp)
        {
            auto* targets = reinterpret_cast<std::vector<std::wstring>*>(GetWindowLongPtrW(dialog, DWLP_USER));
            if (msg == WM_INITDIALOG)
            {
                targets = reinterpret_cast<std::vector<std::wstring>*>(lp);
                SetWindowLongPtrW(dialog, DWLP_USER, lp);
                std::wstring lines;
                for (const auto& name : *targets) lines += name + L"\r\n";
                SetDlgItemTextW(dialog, IDC_TARGET_LIST, lines.c_str());
                SendDlgItemMessageW(dialog, IDC_TARGET_LIST, EM_SETLIMITTEXT, 32768, 0);
                std::set<std::wstring> processes;
                EnumWindows([](HWND hwnd, LPARAM data) -> BOOL
                {
                    if (!IsWindowVisible(hwnd) || GetWindowTextLengthW(hwnd) == 0) return TRUE;
                    DWORD pid = 0;
                    GetWindowThreadProcessId(hwnd, &pid);
                    if (pid == GetCurrentProcessId()) return TRUE;
                    auto name = window::WindowFinder::GetProcessName(pid);
                    if (!name.empty()) reinterpret_cast<std::set<std::wstring>*>(data)->insert(std::move(name));
                    return TRUE;
                }, reinterpret_cast<LPARAM>(&processes));
                for (const auto& name : processes)
                    SendDlgItemMessageW(dialog, IDC_PROCESS_LIST, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
                CenterDialog(dialog);
                return TRUE;
            }
            if (msg == WM_COMMAND && targets)
            {
                if (LOWORD(wp) == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
                if (LOWORD(wp) == IDC_ADD_PROCESS)
                {
                    const auto name = core::NormalizeProcessName(ControlText(dialog, IDC_PROCESS_LIST));
                    if (name.empty())
                    {
                        MessageBoxW(dialog, L"Choose a process or enter an executable name ending in .exe.", L"Target application", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    auto lines = ControlText(dialog, IDC_TARGET_LIST);
                    if (!lines.empty() && lines.back() != L'\n') lines += L"\r\n";
                    lines += name + L"\r\n";
                    SetDlgItemTextW(dialog, IDC_TARGET_LIST, lines.c_str());
                    return TRUE;
                }
                if (LOWORD(wp) == IDOK)
                {
                    std::wistringstream stream(ControlText(dialog, IDC_TARGET_LIST));
                    std::vector<std::wstring> proposed;
                    std::wstring line;
                    while (std::getline(stream, line))
                    {
                        if (line.find_first_not_of(L" \t\r\n") == std::wstring::npos) continue;
                        auto name = core::NormalizeProcessName(line);
                        if (name.empty())
                        {
                            MessageBoxW(dialog, L"Each line must contain an executable name, such as CLIPStudioPaint.exe. Do not enter a full path.", L"Invalid target application", MB_OK | MB_ICONWARNING);
                            return TRUE;
                        }
                        if (std::none_of(proposed.begin(), proposed.end(), [&](const auto& old) { return _wcsicmp(old.c_str(), name.c_str()) == 0; }))
                            proposed.push_back(std::move(name));
                    }
                    *targets = std::move(proposed);
                    EndDialog(dialog, IDOK);
                    return TRUE;
                }
            }
            return FALSE;
        }
    }
    bool EditHotkey(HWND owner, input::Hotkey& key, const std::function<bool(const input::Hotkey&)>& apply)
    {
        HotkeyContext context{key, apply};
        const auto result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_HOTKEY), owner, HotkeyProc, reinterpret_cast<LPARAM>(&context));
        if (result == -1) MessageBoxW(owner, L"Cannot create the shortcut dialog.", L"CSP Grayscale Viewer", MB_OK | MB_ICONERROR);
        if (result != IDOK) return false;
        key = context.Value;
        return true;
    }
    bool EditTargets(HWND owner, std::vector<std::wstring>& targets)
    {
        const auto result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_TARGETS), owner, TargetsProc, reinterpret_cast<LPARAM>(&targets));
        if (result == -1) MessageBoxW(owner, L"Cannot create the target applications dialog.", L"CSP Grayscale Viewer", MB_OK | MB_ICONERROR);
        return result == IDOK;
    }
}
