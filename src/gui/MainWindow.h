#pragma once

#include "core/Application.h"

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QSettings>
#include <QListWidget>
#include <QSpinBox>
#include <QKeySequenceEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QStackedWidget>
#include <QTimer>

namespace csp::gui
{
    class MainWindow : public QMainWindow
    {
        Q_OBJECT

    public:
        explicit MainWindow(csp::core::Application& App, QWidget* Parent = nullptr);
        ~MainWindow() override = default;

    protected:
        void closeEvent(QCloseEvent* Event) override;

    private slots:
        void OnNavClicked(int Index);

        void OnFpsChanged(int Value);

        void OnHotkeyChanged(const QKeySequence& Seq);

        void OnFocusOnlyChanged(bool Checked);

        void OnAutoStartChanged(bool Checked);

        void OnStartMinimizedChanged(bool Checked);

        void OnAddApp();
        void OnRemoveApp();

        void OnTrayActivated(QSystemTrayIcon::ActivationReason Reason);

    private:
        void SetupUi();
        void SetupTray();
        void SetupFocusSync();

        void ApplyStyle();

        void LoadSettings();
        void SaveSettings();

        void UpdateStatusIndicator(bool Enabled, bool Active);

        void UpdateRegistryAutoStart();

        QWidget* CreateGeneralPage();
        QWidget* CreateHotkeyPage();
        QWidget* CreateSystemPage();
        QPushButton* CreateNavButton(const QString& Text, const QString& Icon, int Index);

        void SyncAppListToBackend();

        csp::core::Application& AppRef;

        QVector<QPushButton*> NavButtons;
        QStackedWidget* Stack = nullptr;

        QSpinBox* FpsSpin = nullptr;
        QListWidget* AppRefList = nullptr;

        QKeySequenceEdit* HotkeyEdit = nullptr;
        QCheckBox* FocusOnlyCheck = nullptr;

        QCheckBox* AutoStartCheck = nullptr;
        QCheckBox* StartMinCheck = nullptr;

        QLabel* StatusLabel = nullptr;
        QLabel* StatusDot = nullptr;

        QSystemTrayIcon* Tray = nullptr;
        QTimer* FocusSyncTimer = nullptr;

        QSettings Settings;
    };
}
