#include "tray/NotificationIcon.h"
#include "util/Logger.h"
#include <chrono>
#include <system_error>

namespace csp::tray
{
    bool NotificationIcon::Start(const NOTIFYICONDATAW& data)
    {
        if (Worker.joinable()) return true;
        Base = data;
        Tooltip = data.szTip;
        Stopping = false;
        Generation = 0;
        Revision.store(1);
        AppliedRevision.store(0);
        LongestCall.store(0);
        try { Worker = std::thread(&NotificationIcon::Run, this); }
        catch (const std::system_error& error)
        {
            LOG_ERROR(L"Cannot start notification icon worker (error %d)", error.code().value());
            return false;
        }
        return true;
    }

    void NotificationIcon::UpdateTooltip(const std::wstring& tooltip)
    {
        std::lock_guard lock(Mutex);
        const auto bounded = tooltip.substr(0, _countof(Base.szTip) - 1);
        if (Tooltip == bounded) return;
        Tooltip = bounded;
        Revision.fetch_add(1);
        Wake.notify_one();
    }

    void NotificationIcon::Recreate()
    {
        std::lock_guard lock(Mutex);
        ++Generation;
        Revision.fetch_add(1);
        Wake.notify_one();
    }

    void NotificationIcon::Shutdown()
    {
        {
            std::lock_guard lock(Mutex);
            Stopping = true;
            Wake.notify_one();
        }
        if (Worker.joinable()) Worker.join();
    }

    bool NotificationIcon::Send(DWORD action, NOTIFYICONDATAW& data)
    {
        const ULONGLONG start = GetTickCount64();
        const bool result = CallShell(action, &data) != FALSE;
        const ULONGLONG elapsed = GetTickCount64() - start;
        if (elapsed > LongestCall.load()) LongestCall.store(elapsed);
        if (elapsed >= 50) LOG_DEBUG(L"Background notification icon action %u took %llu ms", action, elapsed);
        return result;
    }

    void NotificationIcon::Run()
    {
        bool added = false;
        bool attempted = false;
        uint64_t applied = 0, generation = 0;
        for (;;)
        {
            NOTIFYICONDATAW data = Base;
            uint64_t revision, requestedGeneration;
            {
                std::unique_lock lock(Mutex);
                if (added)
                    Wake.wait(lock, [&] { return Stopping || Revision.load() != applied; });
                else if (attempted)
                    // Failed registration retries without a UI timer or a busy loop.
                    Wake.wait_for(lock, std::chrono::seconds(2), [&] { return Stopping || Generation != generation; });
                if (Stopping) break;
                revision = Revision.load();
                requestedGeneration = Generation;
                wcsncpy_s(data.szTip, Tooltip.c_str(), _TRUNCATE);
            }
            // No mutex is held while Explorer handles an add/update/delete.
            if (requestedGeneration != generation)
            {
                if (added) Send(NIM_DELETE, data);
                added = false;
                Registered.store(false);
                generation = requestedGeneration;
            }
            bool success;
            if (!added)
            {
                success = Send(NIM_ADD, data);
                if (success)
                {
                    data.uVersion = NOTIFYICON_VERSION_4;
                    success = Send(NIM_SETVERSION, data);
                    if (!success) Send(NIM_DELETE, data);
                }
                added = success;
                attempted = true;
                if (!success) LOG_WARNING(L"Cannot register notification icon; retrying in background");
            }
            else
            {
                data.uFlags = NIF_TIP | NIF_SHOWTIP;
                success = Send(NIM_MODIFY, data);
                if (!success)
                {
                    Send(NIM_DELETE, data);
                    added = false;
                    LOG_WARNING(L"Cannot update notification icon; registering again in background");
                }
            }
            Registered.store(added);
            if (success)
            {
                applied = revision;
                AppliedRevision.store(applied);
            }
        }
        if (added)
        {
            auto data = Base;
            Send(NIM_DELETE, data);
        }
        Registered.store(false);
        LOG_INFO(L"Notification icon worker stopped; longest Shell call: %llu ms", LongestCall.load());
    }
}
