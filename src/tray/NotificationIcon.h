#pragma once
#include <Windows.h>
#include <shellapi.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace csp::tray
{
    // Explorer can block Shell_NotifyIcon for hundreds of milliseconds while a
    // fullscreen window changes. Serialize those calls away from the hotkey UI.
    class NotificationIcon
    {
    public:
        using ShellCall = std::function<BOOL(DWORD, PNOTIFYICONDATAW)>;
        explicit NotificationIcon(ShellCall call = Shell_NotifyIconW) : CallShell(std::move(call)) {}
        ~NotificationIcon() { Shutdown(); }
        NotificationIcon(const NotificationIcon&) = delete;
        NotificationIcon& operator=(const NotificationIcon&) = delete;

        bool Start(const NOTIFYICONDATAW& data);
        void UpdateTooltip(const std::wstring& tooltip);
        void Recreate();
        // Join before destroying the callback HWND or its HICON.
        void Shutdown();
        bool IsRegistered() const { return Registered.load(); }
        bool IsSynchronized() const { return Registered.load() && AppliedRevision.load() == Revision.load(); }
        ULONGLONG LongestCallMilliseconds() const { return LongestCall.load(); }

    private:
        void Run();
        bool Send(DWORD action, NOTIFYICONDATAW& data);
        ShellCall CallShell;
        std::thread Worker;
        std::mutex Mutex;
        std::condition_variable Wake;
        NOTIFYICONDATAW Base{};
        std::wstring Tooltip;
        bool Stopping = false;
        uint64_t Generation = 0;
        std::atomic<uint64_t> Revision{0};
        std::atomic<uint64_t> AppliedRevision{0};
        std::atomic<bool> Registered{false};
        std::atomic<ULONGLONG> LongestCall{0};
    };
}
