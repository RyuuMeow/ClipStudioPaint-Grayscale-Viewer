#include "core/Application.h"
#include "util/Logger.h"

namespace csp::core
{
    namespace
    {
        constexpr int kHotkeyToggle = 1;
    }

    Application::~Application()
    {
        if (MagOverlay.IsVisible())
        {
            MagOverlay.Hide();
            MagOverlay.Detach();
        }
        FocusTracker.Stop();
        UnregisterHotkeyNow();
        MagOverlay.Shutdown();
    }

    bool Application::Initialize()
    {
        LOG_INFO(L"CSP Grayscale Viewer v1.0 initializing...");

        if (!MagOverlay.Initialize())
        {
            LOG_ERROR(L"Failed to initialize MagnifierOverlay");
            return false;
        }

        if (!FocusTracker.Start(
            [this](HWND hwnd, bool isTarget)
        { OnFocusChanged(hwnd, isTarget); }))
        {
            LOG_ERROR(L"Failed to start focus tracker");
            return false;
        }

        HWND fg = GetForegroundWindow();
        bIsTargetInFocus = (fg && window::WindowFinder::IsTargetWindow(fg));

        if (bHotkeyFocusOnly)
        {
            if (bIsTargetInFocus)
            {
                RegisterHotkeyNow();
            }
            else
            {
                LOG_INFO(L"Hotkey will register when a target app gains focus");
            }
        }
        else
        {
            RegisterHotkeyNow();
        }

        LOG_INFO(L"Initialized. Press hotkey to toggle grayscale.");
        return true;
    }

    int Application::RunConsole()
    {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0))
        {
            if (msg.message == WM_HOTKEY)
            {
                HotkeyManager.HandleHotkey(static_cast<int>(msg.wParam));
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
    }

    void Application::Quit()
    {
        PostThreadMessageW(GetCurrentThreadId(), WM_QUIT, 0, 0);
    }

    void Application::HandleNativeHotkey(int Id)
    {
        HotkeyManager.HandleHotkey(Id);
    }

    void Application::ToggleGrayscale()
    {
        bIsGrayscaleEnabled = !bIsGrayscaleEnabled;

        if (bIsGrayscaleEnabled)
        {
            LOG_INFO(L"Grayscale ENABLED");

            if (bIsTargetInFocus)
            {
                HWND fg = GetForegroundWindow();
                if (fg && window::WindowFinder::IsTargetWindow(fg))
                {
                    HWND rootHwnd = GetAncestor(fg, GA_ROOTOWNER);
                    MagOverlay.Attach(rootHwnd ? rootHwnd : fg);
                    MagOverlay.Show();
                    LOG_INFO(L"Applied to focused target window");
                }
            }
            else
            {
                LOG_INFO(L"Waiting for a target app to gain focus...");
            }
        }
        else
        {
            LOG_INFO(L"Grayscale DISABLED");
            MagOverlay.Hide();
            MagOverlay.Detach();
        }
        NotifyState();
    }

    void Application::SetRefreshRate(int Fps)
    {
        MagOverlay.SetRefreshRate(Fps);
    }

    void Application::SetHotkey(UINT Modifiers, UINT Vk)
    {
        if (bHotkeyRegistered)
        {
            UnregisterHotkeyNow();
        }

        HotkeyMod = Modifiers;
        HotkeyVk = Vk;

        bool shouldRegister = !bHotkeyFocusOnly || bIsTargetInFocus;
        if (shouldRegister)
        {
            RegisterHotkeyNow();
        }
    }

    void Application::SetHotkeyFocusOnly(bool Enabled)
    {
        if (bHotkeyFocusOnly == Enabled)
        {
            return;
        }
        bHotkeyFocusOnly = Enabled;

        if (Enabled)
        {
            if (!bIsTargetInFocus && bHotkeyRegistered)
            {
                UnregisterHotkeyNow();
            }
        }
        else
        {
            if (!bHotkeyRegistered)
            {
                RegisterHotkeyNow();
            }
        }
    }

    void Application::SetTargetApps(const std::vector<std::wstring>& Apps)
    {
        window::WindowFinder::SetTargetProcessNames(Apps);
    }

    void Application::ReevaluateFocus()
    {
        HWND fg = GetForegroundWindow();
        bool isTarget = (fg && window::WindowFinder::IsTargetWindow(fg));
        OnFocusChanged(fg, isTarget);
    }

    void Application::OnFocusChanged(HWND Hwnd, bool IsTarget)
    {
        if (IsTarget && Hwnd)
        {
            HWND rootHwnd = GetAncestor(Hwnd, GA_ROOTOWNER);
            if (rootHwnd)
            {
                Hwnd = rootHwnd;
            }
        }

        bool wasTargetFocused = bIsTargetInFocus;
        bIsTargetInFocus = IsTarget;

        if (bHotkeyFocusOnly)
        {
            if (IsTarget)
            {
                if (!bHotkeyRegistered)
                {
                    RegisterHotkeyNow();
                }
            }
            else
            {
                if (bHotkeyRegistered)
                {
                    UnregisterHotkeyNow();
                }
            }
        }

        if (!bIsGrayscaleEnabled)
        {
            return;
        }

        if (IsTarget && !wasTargetFocused)
        {
            MagOverlay.Attach(Hwnd);
            MagOverlay.Show();
            LOG_DEBUG(L"Target focused, overlay attached");
        }
        else if (!IsTarget && wasTargetFocused)
        {
            MagOverlay.Hide();
            LOG_DEBUG(L"Target unfocused, overlay hidden");
        }
        else if (IsTarget && wasTargetFocused)
        {
            if (MagOverlay.TargetWindow() != Hwnd)
            {
                MagOverlay.Hide();
                MagOverlay.Attach(Hwnd);
                MagOverlay.Show();
                LOG_DEBUG(L"Switched target window, overlay re-attached");
            }
        }
        NotifyState();
    }

    void Application::RegisterHotkeyNow()
    {
        if (bHotkeyRegistered)
        {
            return;
        }
        if (HotkeyManager.Register(kHotkeyToggle, HotkeyMod, HotkeyVk,
                                   [this]()
        { ToggleGrayscale(); }))
        {
            bHotkeyRegistered = true;
        }
        else
        {
            LOG_ERROR(L"Failed to register toggle hotkey. Will retry on next focus change.");
        }
    }

    void Application::UnregisterHotkeyNow()
    {
        if (!bHotkeyRegistered)
        {
            return;
        }
        HotkeyManager.UnregisterAll();
        bHotkeyRegistered = false;
    }

    void Application::NotifyState()
    {
        if (StateChangeCallback)
        {
            StateChangeCallback(bIsGrayscaleEnabled, MagOverlay.IsVisible());
        }
    }
}
