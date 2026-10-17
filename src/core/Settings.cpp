#include "core/Settings.h"
#include <Windows.h>
#include <shlobj.h>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <map>
#include <sstream>

namespace csp::core
{
    namespace
    {
        std::wstring Trim(std::wstring value)
        {
            const auto first = value.find_first_not_of(L" \r\n\t");
            if (first == std::wstring::npos) return {};
            return value.substr(first, value.find_last_not_of(L" \r\n\t") - first + 1);
        }
        // Qt INI strings can be quoted and contain hexadecimal UTF-16 escapes.
        std::vector<std::wstring> DecodeList(const std::wstring& value)
        {
            if (value.empty() || value == L"@Invalid()" || value == L"@StringList()") return {};
            std::vector<std::wstring> result;
            std::wstring item;
            bool quoted = false;
            for (size_t i = 0; i < value.size(); ++i)
            {
                wchar_t c = value[i];
                if (c == L'"') { quoted = !quoted; continue; }
                if (c == L',' && !quoted) { result.push_back(Trim(item)); item.clear(); continue; }
                if (c == L'\\' && i + 1 < value.size())
                {
                    c = value[++i];
                    if (c == L'x')
                    {
                        unsigned code = 0;
                        size_t digits = 0;
                        while (i + 1 < value.size() && digits < 4 && iswxdigit(value[i + 1]))
                        {
                            const wchar_t digit = static_cast<wchar_t>(towlower(value[++i]));
                            code = code * 16 + (digit <= L'9' ? digit - L'0' : digit - L'a' + 10);
                            ++digits;
                        }
                        c = static_cast<wchar_t>(code);
                    }
                    else if (c == L'n') c = L'\n';
                    else if (c == L'r') c = L'\r';
                    else if (c == L't') c = L'\t';
                }
                item += c;
            }
            if (quoted) throw std::invalid_argument("unterminated string");
            result.push_back(Trim(item));
            return result;
        }
        std::wstring Quote(const std::wstring& value)
        {
            std::wstring result = L"\"";
            for (const auto c : value)
            {
                if (c == L'\\' || c == L'"') result += L'\\';
                result += c;
            }
            return result + L'"';
        }
    }

    std::wstring NormalizeProcessName(std::wstring name)
    {
        name = Trim(std::move(name));
        if (name.size() < 5 || name.size() > 255 ||
            name.find_first_of(L"\\/:*?\"<>|\r\n\t") != std::wstring::npos ||
            name.find(L'\0') != std::wstring::npos ||
            _wcsicmp(name.c_str() + name.size() - 4, L".exe") != 0) return {};
        return name;
    }

    bool ParseSettings(const std::wstring& text, Settings& result, std::wstring& error)
    {
        std::map<std::wstring, std::wstring> values;
        std::wistringstream stream(text);
        std::wstring line, section;
        while (std::getline(stream, line))
        {
            line = Trim(line);
            if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
            if (line.front() == L'[' && line.back() == L']')
            {
                section = line.substr(1, line.size() - 2);
                if (section == L"%General") section = L"General";
                continue;
            }
            const auto equals = line.find(L'=');
            if (equals == std::wstring::npos) { error = L"Invalid settings line."; return false; }
            values[section + L"/" + Trim(line.substr(0, equals))] = Trim(line.substr(equals + 1));
        }
        Settings parsed;
        try
        {
            auto number = [&](const wchar_t* key, int fallback)
            {
                const auto it = values.find(key);
                if (it == values.end()) return fallback;
                size_t used = 0;
                int n = std::stoi(it->second, &used);
                if (used != it->second.size()) throw std::invalid_argument("number");
                return n;
            };
            auto boolean = [&](const wchar_t* key, bool fallback)
            {
                const auto it = values.find(key);
                if (it == values.end()) return fallback;
                if (it->second == L"true" || it->second == L"1") return true;
                if (it->second == L"false" || it->second == L"0") return false;
                throw std::invalid_argument("boolean");
            };
            const int version = number(L"App/Version", 0);
            if (version < 0 || version > 1) throw std::invalid_argument("version");
            parsed.RefreshRate = number(L"General/RefreshRate", 60);
            if (parsed.RefreshRate != 0 && (parsed.RefreshRate < 15 || parsed.RefreshRate > 144))
                throw std::invalid_argument("refresh rate");
            parsed.FocusOnly = boolean(L"Hotkey/FocusOnly", true);
            parsed.AutoStart = boolean(L"System/AutoStart", false);
            parsed.UseD3D = boolean(L"Renderer/UseD3D", version == 0 ? false : parsed.UseD3D);
            if (version == 1)
            {
                parsed.ToggleHotkey.Modifiers = static_cast<UINT>(number(L"Hotkey/Modifiers", 0));
                parsed.ToggleHotkey.Key = static_cast<UINT>(number(L"Hotkey/Key", VK_F9));
                if (!input::IsSupportedHotkey(parsed.ToggleHotkey)) throw std::invalid_argument("hotkey");
            }
            else if (const auto it = values.find(L"Hotkey/Sequence"); it != values.end())
            {
                auto decoded = DecodeList(it->second);
                auto hotkey = decoded.empty() ? std::nullopt : input::ParseHotkey(decoded.front());
                if (!hotkey) throw std::invalid_argument("legacy hotkey");
                parsed.ToggleHotkey = *hotkey;
            }
            if (const auto it = values.find(L"General/TargetApps"); it != values.end())
            {
                parsed.TargetApps.clear();
                for (auto app : DecodeList(it->second))
                {
                    app = NormalizeProcessName(std::move(app));
                    if (app.empty()) throw std::invalid_argument("target process");
                    if (std::none_of(parsed.TargetApps.begin(), parsed.TargetApps.end(), [&](const auto& old)
                        { return _wcsicmp(old.c_str(), app.c_str()) == 0; })) parsed.TargetApps.push_back(std::move(app));
                }
            }
        }
        catch (const std::exception&)
        {
            error = L"Invalid or unsupported settings. The original file was not changed.";
            return false;
        }
        result = std::move(parsed);
        error.clear();
        return true;
    }

    std::wstring SerializeSettings(const Settings& value)
    {
        std::wostringstream out;
        out << L"[App]\nVersion=1\n[General]\nRefreshRate=" << value.RefreshRate << L"\nTargetApps=";
        for (size_t i = 0; i < value.TargetApps.size(); ++i) out << (i ? L", " : L"") << Quote(value.TargetApps[i]);
        out << L"\n[Hotkey]\nModifiers=" << value.ToggleHotkey.Modifiers
            << L"\nKey=" << value.ToggleHotkey.Key << L"\nFocusOnly=" << value.FocusOnly
            << L"\n[System]\nAutoStart=" << value.AutoStart
            << L"\n[Renderer]\nUseD3D=" << value.UseD3D << L"\n";
        return out.str();
    }

    bool LoadSettings(const std::filesystem::path& path, Settings& result, std::wstring& error)
    {
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec || size > 1024 * 1024) { error = L"Cannot read settings file or file is too large."; return false; }
        std::ifstream file(path, std::ios::binary);
        if (!file) { error = L"Cannot read settings file."; return false; }
        const std::string bytes((std::istreambuf_iterator<char>(file)), {});
        std::wstring text;
        if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff && static_cast<unsigned char>(bytes[1]) == 0xfe)
        {
            if (bytes.size() % 2) { error = L"Invalid UTF-16 settings."; return false; }
            for (size_t i = 2; i < bytes.size(); i += 2)
                text += static_cast<wchar_t>(static_cast<unsigned char>(bytes[i]) | (static_cast<unsigned char>(bytes[i + 1]) << 8));
        }
        else
        {
            const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
            if (!length && !bytes.empty()) { error = L"Invalid UTF-8 settings."; return false; }
            text.resize(length);
            if (length) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), text.data(), length);
            if (!text.empty() && text.front() == 0xfeff) text.erase(text.begin());
        }
        return ParseSettings(text, result, error);
    }

    bool SaveSettings(const std::filesystem::path& path, const Settings& value, std::wstring& error)
    {
        const auto text = SerializeSettings(value);
        Settings validated;
        if (!ParseSettings(text, validated, error)) return false;
        if (validated != value) { error = L"Settings contain invalid or duplicate values."; return false; }
        std::error_code ec;
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) { error = L"Cannot create settings directory."; return false; }
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (!size) { error = L"Cannot encode settings."; return false; }
        std::string bytes(size, '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), bytes.data(), size, nullptr, nullptr);
        const auto temporary = std::filesystem::path(path.wstring() + L".tmp");
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            file.close();
            if (!file) { error = L"Cannot write settings."; return false; }
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            error = L"Cannot replace settings file (error " + std::to_wstring(GetLastError()) + L").";
            return false;
        }
        error.clear();
        return true;
    }

    std::filesystem::path SettingsDirectory()
    {
        PWSTR path = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path))) return {};
        std::filesystem::path result(path);
        CoTaskMemFree(path);
        return result / L"CSP-Grayscale-Viewer";
    }
}
