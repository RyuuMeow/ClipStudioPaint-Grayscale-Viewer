#include "core/Application.h"
#include "gui/MainWindow.h"
#include "util/Logger.h"

#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <Windows.h>


class HotkeyFilter : public QAbstractNativeEventFilter
{
public:
    explicit HotkeyFilter(csp::core::Application& app) : App(app)
    {}

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr*) override
    {
        if (eventType == "windows_generic_MSG")
        {
            auto* msg = static_cast<MSG*>(message);
            if (msg->message == WM_HOTKEY)
            {
                App.HandleNativeHotkey(static_cast<int>(msg->wParam));
                return true;
            }
        }
        return false;
    }

private:
    csp::core::Application& App;
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR lpCmdLine, int)
{

    int Argc = 0;
    QApplication qtApp(Argc, nullptr);
    qtApp.setQuitOnLastWindowClosed(false);

    csp::core::Application App;
    if (!App.Initialize())
    {
        LOG_ERROR(L"Initialization failed.");
        return 1;
    }

    HotkeyFilter Filter(App);
    qtApp.installNativeEventFilter(&Filter);

    csp::gui::MainWindow Window(App);

    std::wstring CmdLine(lpCmdLine ? lpCmdLine : L"");
    if (CmdLine.find(L"--minimized") == std::wstring::npos)
    {
        Window.show();
    }

    return qtApp.exec();
}
