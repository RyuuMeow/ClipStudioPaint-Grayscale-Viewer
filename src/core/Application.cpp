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
        HideActiveOverlay();
        DetachActiveOverlay();
        FocusTracker.Stop();
        UnregisterHotkeyNow();
        D3DOverlay.Shutdown();
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
                    AttachActiveOverlay(rootHwnd ? rootHwnd : fg);
                    ShowActiveOverlay();
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
            HideActiveOverlay();
            DetachActiveOverlay();
        }
        NotifyState();
    }

    void Application::SetRefreshRate(int Fps)
    {
        RefreshRate = Fps;
        MagOverlay.SetRefreshRate(Fps);
        D3DOverlay.SetRefreshRate(Fps);
    }

    void Application::SetD3DRendererEnabled(bool Enabled)
    {
        if (bUseD3DRenderer == Enabled)
        {
            return;
        }

        HWND activeTarget = ActiveOverlayTargetWindow();
        const bool shouldRestore = bIsGrayscaleEnabled && bIsTargetInFocus && activeTarget;

        HideActiveOverlay();
        DetachActiveOverlay();

        if (Enabled && !D3DOverlay.IsInitialized())
        {
            D3DOverlay.SetRefreshRate(RefreshRate);
            if (!D3DOverlay.Initialize())
            {
                LOG_ERROR(L"Failed to initialize experimental D3D renderer; keeping Magnification renderer");
                bUseD3DRenderer = false;
                if (shouldRestore)
                {
                    AttachActiveOverlay(activeTarget);
                    ShowActiveOverlay();
                }
                NotifyState();
                return;
            }
        }

        bUseD3DRenderer = Enabled;
        LOG_INFO(L"Renderer backend: %s", bUseD3DRenderer ? L"Direct3D experimental" : L"Magnification");

        if (shouldRestore)
        {
            AttachActiveOverlay(activeTarget);
            ShowActiveOverlay();
        }

        NotifyState();
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
            ReevaluateFocus();
            return;
        }
        bHotkeyFocusOnly = Enabled;

        ReevaluateFocus();

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
        ReevaluateFocus();
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
            NotifyState();
            return;
        }

        if (IsTarget && !wasTargetFocused)
        {
            AttachActiveOverlay(Hwnd);
            ShowActiveOverlay();
            LOG_DEBUG(L"Target focused, overlay attached");
        }
        else if (!IsTarget && wasTargetFocused)
        {
            HideActiveOverlay();
            LOG_DEBUG(L"Target unfocused, overlay hidden");
        }
        else if (IsTarget && wasTargetFocused)
        {
            if (ActiveOverlayTargetWindow() != Hwnd)
            {
                HideActiveOverlay();
                AttachActiveOverlay(Hwnd);
                ShowActiveOverlay();
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

    void Application::AttachActiveOverlay(HWND Hwnd)
    {
        if (bUseD3DRenderer)
        {
            D3DOverlay.Attach(Hwnd);
        }
        else
        {
            MagOverlay.Attach(Hwnd);
        }
    }

    void Application::DetachActiveOverlay()
    {
        if (bUseD3DRenderer)
        {
            D3DOverlay.Detach();
        }
        else
        {
            MagOverlay.Detach();
        }
    }

    void Application::HideActiveOverlay()
    {
        if (bUseD3DRenderer)
        {
            D3DOverlay.Hide();
        }
        else
        {
            MagOverlay.Hide();
        }
    }

    void Application::ShowActiveOverlay()
    {
        if (bUseD3DRenderer)
        {
            D3DOverlay.Show();
        }
        else
        {
            MagOverlay.Show();
        }
    }

    HWND Application::ActiveOverlayTargetWindow() const
    {
        return bUseD3DRenderer ? D3DOverlay.TargetWindow() : MagOverlay.TargetWindow();
    }

    bool Application::IsActiveOverlayVisible() const
    {
        return bUseD3DRenderer ? D3DOverlay.IsVisible() : MagOverlay.IsVisible();
    }

    void Application::NotifyState()
    {
        if (StateChangeCallback)
        {
            StateChangeCallback(bIsGrayscaleEnabled, IsActiveOverlayVisible());
        }
    }
}
