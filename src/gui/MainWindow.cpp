#include "gui/MainWindow.h"
#include "util/Logger.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QLineEdit>
#include <QInputDialog>
#include <QMenu>
#include <QAction>
#include <QCloseEvent>
#include <QStyle>
#include <QTimer>
#include <QScreen>
#include <QScrollBar>
#include <QSlider>
#include <QFrame>
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QPlainTextEdit>
#include <QProcess>

#include <Windows.h>
#include <Psapi.h>
#include <set>
#pragma comment(lib, "Psapi.lib")

namespace csp::gui
{
    // === Dark Theme ===

    namespace
    {
        const QString kStyleSheet = R"(
            * {
                font-family: 'Segoe UI', 'Microsoft JhengHei UI', sans-serif;
                font-size: 13px;
            }

            QMainWindow {
                background: #0d1117;
            }

            /* ─── Sidebar ─── */
            QWidget#sidebar {
                background: #0d1117;
                border-right: 1px solid #21262d;
            }

            QPushButton#navBtn {
                text-align: left;
                padding: 10px 16px;
                border: none;
                border-radius: 8px;
                color: #8b949e;
                font-size: 13px;
                font-weight: 500;
                margin: 2px 8px;
            }
            QPushButton#navBtn:hover {
                background: #161b22;
                color: #c9d1d9;
            }
            QPushButton#navBtn:checked {
                background: #1c2333;
                color: #58a6ff;
            }

            /* ─── Content ─── */
            QWidget#contentArea {
                background: #0d1117;
            }

            QLabel#pageTitle {
                font-size: 20px;
                font-weight: 700;
                color: #e6edf3;
                padding: 0 0 4px 0;
            }

            QLabel#pageDesc {
                font-size: 12px;
                color: #8b949e;
                padding: 0 0 12px 0;
            }

            QLabel {
                color: #c9d1d9;
            }

            /* ─── Group boxes ─── */
            QGroupBox {
                background: #161b22;
                border: 1px solid #21262d;
                border-radius: 10px;
                margin-top: 8px;
                padding: 20px 16px 12px 16px;
                font-weight: 600;
                color: #e6edf3;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                subcontrol-position: top left;
                padding: 2px 12px;
                color: #e6edf3;
            }

            /* ─── Inputs ─── */
            QSpinBox, QKeySequenceEdit, QLineEdit {
                background: #0d1117;
                border: 1px solid #30363d;
                border-radius: 6px;
                padding: 6px 10px;
                color: #e6edf3;
                selection-background-color: #264f78;
            }
            QSpinBox:focus, QKeySequenceEdit:focus, QLineEdit:focus {
                border-color: #58a6ff;
            }
            QSpinBox::up-button, QSpinBox::down-button {
                background: #21262d;
                border: none;
                border-radius: 3px;
                width: 18px;
            }
            QSpinBox::up-button:hover, QSpinBox::down-button:hover {
                background: #30363d;
            }

            /* ─── Buttons ─── */
            QPushButton#actionBtn {
                background: #21262d;
                border: 1px solid #30363d;
                border-radius: 6px;
                padding: 6px 16px;
                color: #c9d1d9;
                font-weight: 500;
            }
            QPushButton#actionBtn:hover {
                background: #30363d;
                border-color: #58a6ff;
                color: #e6edf3;
            }
            QPushButton#actionBtn:pressed {
                background: #1c2333;
            }

            QPushButton#primaryBtn {
                background: #238636;
                border: 1px solid #2ea043;
                border-radius: 6px;
                padding: 6px 16px;
                color: #ffffff;
                font-weight: 600;
            }
            QPushButton#primaryBtn:hover {
                background: #2ea043;
            }

            /* ─── List widget ─── */
            QListWidget {
                background: #0d1117;
                border: 1px solid #30363d;
                border-radius: 6px;
                padding: 4px;
                color: #c9d1d9;
                outline: none;
            }
            QListWidget::item {
                padding: 6px 8px;
                border-radius: 4px;
            }
            QListWidget::item:selected {
                background: #1c2333;
                color: #58a6ff;
            }
            QListWidget::item:hover {
                background: #161b22;
            }

            /* ─── Checkboxes ─── */
            QCheckBox {
                color: #c9d1d9;
                spacing: 8px;
            }
            QCheckBox::indicator {
                width: 18px;
                height: 18px;
                border: 2px solid #30363d;
                border-radius: 4px;
                background: #0d1117;
            }
            QCheckBox::indicator:checked {
                background: #58a6ff;
                border-color: #58a6ff;
            }
            QCheckBox::indicator:hover {
                border-color: #58a6ff;
            }

            /* ─── Slider ─── */
            QSlider::groove:horizontal {
                height: 4px;
                background: #21262d;
                border-radius: 2px;
            }
            QSlider::handle:horizontal {
                width: 16px;
                height: 16px;
                margin: -6px 0;
                background: #58a6ff;
                border-radius: 8px;
            }
            QSlider::handle:horizontal:hover {
                background: #79c0ff;
            }
            QSlider::sub-page:horizontal {
                background: #58a6ff;
                border-radius: 2px;
            }

            /* ─── Separator ─── */
            QFrame#separator {
                background: #21262d;
                max-height: 1px;
            }

            /* ─── Status bar area ─── */
            QWidget#statusBar {
                background: #0d1117;
                border-top: 1px solid #21262d;
            }

            QPlainTextEdit {
                background: #0d1117;
                border: 1px solid #30363d;
                border-radius: 6px;
                padding: 8px;
                color: #c9d1d9;
                font-family: 'Cascadia Mono', 'Consolas', monospace;
                font-size: 12px;
                selection-background-color: #264f78;
            }
        )";
    }

    MainWindow::MainWindow(csp::core::Application& App, QWidget* Parent)
        : QMainWindow(Parent)
          , AppRef(App)
          , Settings(QSettings::IniFormat, QSettings::UserScope,
                       "CSP-Grayscale-Viewer", "settings")
          {
        setWindowTitle("CSP Grayscale Viewer");
        setMinimumSize(620, 460);
        resize(660, 500);

        ApplyStyle();
        SetupUi();
        SetupTray();
        LoadSettings();
        SetupFocusSync();
        SetupLogSink();

        QTimer::singleShot(0, this, [this]()
        {
            AppRef.ReevaluateFocus();
        });

        AppRef.SetStateCallback([this](bool enabled, bool active)
        {
            QMetaObject::invokeMethod(this, [this, enabled, active]()
            {
                UpdateStatusIndicator(enabled, active);
            }, Qt::QueuedConnection);
        });
    }

    MainWindow::~MainWindow()
    {
        csp::util::Logger::Instance().SetSink(nullptr);
    }

    void MainWindow::ApplyStyle()
    {
        qApp->setStyleSheet(kStyleSheet);
    }

    void MainWindow::SetupUi()
    {
        auto* central = new QWidget;
        setCentralWidget(central);

        auto* mainLayout = new QHBoxLayout(central);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        auto* sidebar = new QWidget;
        sidebar->setObjectName("sidebar");
        sidebar->setFixedWidth(180);

        auto* sideLayout = new QVBoxLayout(sidebar);
        sideLayout->setContentsMargins(0, 16, 0, 12);
        sideLayout->setSpacing(0);

        sideLayout->addWidget(CreateNavButton(QString::fromWCharArray(L"\u2699  General"), "", 0));
        sideLayout->addWidget(CreateNavButton(QString::fromWCharArray(L"\u2328  Hotkey"), "", 1));
        sideLayout->addWidget(CreateNavButton(QString::fromWCharArray(L"\U0001F5A5  System"), "", 2));
        sideLayout->addWidget(CreateNavButton("\U0001F4C4  Log", "", 3));

        sideLayout->addStretch();

        auto* statusWidget = new QWidget;
        statusWidget->setObjectName("statusBar");
        auto* statusLayout = new QHBoxLayout(statusWidget);
        statusLayout->setContentsMargins(16, 10, 16, 10);

        StatusDot = new QLabel;
        StatusDot->setFixedSize(10, 10);
        StatusDot->setStyleSheet("background: #30363d; border-radius: 5px;");

        StatusLabel = new QLabel("Inactive");
        StatusLabel->setStyleSheet("color: #8b949e; font-size: 12px;");

        statusLayout->addWidget(StatusDot);
        statusLayout->addWidget(StatusLabel);
        statusLayout->addStretch();
        sideLayout->addWidget(statusWidget);

        mainLayout->addWidget(sidebar);

        auto* contentWidget = new QWidget;
        contentWidget->setObjectName("contentArea");
        auto* contentLayout = new QVBoxLayout(contentWidget);
        contentLayout->setContentsMargins(24, 20, 24, 20);

        Stack = new QStackedWidget;
        Stack->addWidget(CreateGeneralPage());
        Stack->addWidget(CreateHotkeyPage());
        Stack->addWidget(CreateSystemPage());
        Stack->addWidget(CreateLogPage());

        contentLayout->addWidget(Stack);
        mainLayout->addWidget(contentWidget, 1);

        if (!NavButtons.isEmpty())
        {
            NavButtons[0]->setChecked(true);
        }
    }

    QPushButton* MainWindow::CreateNavButton(const QString& Text, const QString&, int Index)
    {
        auto* btn = new QPushButton(Text);
        btn->setObjectName("navBtn");
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        NavButtons.append(btn);
        connect(btn, &QPushButton::clicked, this, [this, Index]()
        { OnNavClicked(Index); });
        return btn;
    }

    QWidget* MainWindow::CreateGeneralPage()
    {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);

        auto* title = new QLabel("General");
        title->setObjectName("pageTitle");
        layout->addWidget(title);

        auto* desc = new QLabel("Configure refresh rate and target applications.");
        desc->setObjectName("pageDesc");
        layout->addWidget(desc);

        auto* fpsGroup = new QGroupBox("Refresh Rate");
        auto* fpsLayout = new QHBoxLayout(fpsGroup);

        auto* fpsSlider = new QSlider(Qt::Horizontal);
        fpsSlider->setRange(15, 144);
        fpsSlider->setValue(60);

        FpsSpin = new QSpinBox;
        FpsSpin->setRange(15, 144);
        FpsSpin->setValue(60);
        FpsSpin->setSuffix(" fps");
        FpsSpin->setFixedWidth(100);

        connect(fpsSlider, &QSlider::valueChanged, FpsSpin, &QSpinBox::setValue);
        connect(FpsSpin, QOverload<int>::of(&QSpinBox::valueChanged), fpsSlider, &QSlider::setValue);
        connect(FpsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::OnFpsChanged);

        fpsLayout->addWidget(fpsSlider, 1);
        fpsLayout->addWidget(FpsSpin);
        layout->addWidget(fpsGroup);

        auto* appGroup = new QGroupBox("Target Applications");
        auto* appLayout = new QVBoxLayout(appGroup);

        AppRefList = new QListWidget;
        AppRefList->setMaximumHeight(150);
        appLayout->addWidget(AppRefList);

        auto* btnLayout = new QHBoxLayout;
        auto* addBtn = new QPushButton("+ Add Process");
        addBtn->setObjectName("primaryBtn");
        addBtn->setCursor(Qt::PointingHandCursor);
        connect(addBtn, &QPushButton::clicked, this, &MainWindow::OnAddApp);

        auto* removeBtn = new QPushButton("Remove");
        removeBtn->setObjectName("actionBtn");
        removeBtn->setCursor(Qt::PointingHandCursor);
        connect(removeBtn, &QPushButton::clicked, this, &MainWindow::OnRemoveApp);

        btnLayout->addWidget(addBtn);
        btnLayout->addWidget(removeBtn);
        btnLayout->addStretch();
        appLayout->addLayout(btnLayout);

        layout->addWidget(appGroup);
        layout->addStretch();

        return page;
    }

    QWidget* MainWindow::CreateLogPage()
    {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);

        auto* title = new QLabel("Log");
        title->setObjectName("pageTitle");
        layout->addWidget(title);

        auto* desc = new QLabel("Runtime diagnostics for renderer, focus, and hotkey behavior.");
        desc->setObjectName("pageDesc");
        layout->addWidget(desc);

        LogText = new QPlainTextEdit;
        LogText->setReadOnly(true);
        LogText->setMaximumBlockCount(1000);
        layout->addWidget(LogText, 1);

        auto* btnLayout = new QHBoxLayout;
        auto* clearBtn = new QPushButton("Clear");
        clearBtn->setObjectName("actionBtn");
        clearBtn->setCursor(Qt::PointingHandCursor);
        connect(clearBtn, &QPushButton::clicked, this, [this]()
        {
            if (LogText)
            {
                LogText->clear();
            }
        });

        btnLayout->addWidget(clearBtn);
        btnLayout->addStretch();
        layout->addLayout(btnLayout);

        return page;
    }

    QWidget* MainWindow::CreateHotkeyPage()
    {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);

        auto* title = new QLabel("Hotkey");
        title->setObjectName("pageTitle");
        layout->addWidget(title);

        auto* desc = new QLabel("Configure the keyboard shortcut to toggle grayscale.");
        desc->setObjectName("pageDesc");
        layout->addWidget(desc);

        auto* hotkeyGroup = new QGroupBox("Toggle Grayscale");
        auto* hkLayout = new QVBoxLayout(hotkeyGroup);

        auto* hkRow = new QHBoxLayout;
        auto* hkLabel = new QLabel("Shortcut:");
        HotkeyEdit = new QKeySequenceEdit(QKeySequence(Qt::Key_F9));
        HotkeyEdit->setMaximumSequenceLength(1);
        HotkeyEdit->setFixedWidth(200);
        connect(HotkeyEdit, &QKeySequenceEdit::keySequenceChanged,
                this, &MainWindow::OnHotkeyChanged);

        hkRow->addWidget(hkLabel);
        hkRow->addWidget(HotkeyEdit);
        hkRow->addStretch();
        hkLayout->addLayout(hkRow);

        hkLayout->addSpacing(8);

        FocusOnlyCheck = new QCheckBox("Only register hotkey when a target app is focused");
        FocusOnlyCheck->setChecked(true);
        connect(FocusOnlyCheck, &QCheckBox::toggled,
                this, &MainWindow::OnFocusOnlyChanged);
        hkLayout->addWidget(FocusOnlyCheck);

        layout->addWidget(hotkeyGroup);
        layout->addStretch();

        return page;
    }

    QWidget* MainWindow::CreateSystemPage()
    {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);

        auto* title = new QLabel("System");
        title->setObjectName("pageTitle");
        layout->addWidget(title);

        auto* desc = new QLabel("System integration and startup behavior.");
        desc->setObjectName("pageDesc");
        layout->addWidget(desc);

        auto* startupGroup = new QGroupBox("Startup");
        auto* startLayout = new QVBoxLayout(startupGroup);

        AutoStartCheck = new QCheckBox("Start with Windows");
        connect(AutoStartCheck, &QCheckBox::toggled,
                this, &MainWindow::OnAutoStartChanged);
        startLayout->addWidget(AutoStartCheck);

        StartMinCheck = new QCheckBox("Start minimized to system tray");
        connect(StartMinCheck, &QCheckBox::toggled,
                this, &MainWindow::OnStartMinimizedChanged);
        startLayout->addWidget(StartMinCheck);

        layout->addWidget(startupGroup);

        auto* rendererGroup = new QGroupBox("Renderer");
        auto* rendererLayout = new QVBoxLayout(rendererGroup);

        D3DRendererCheck = new QCheckBox("Use experimental Direct3D renderer");
        connect(D3DRendererCheck, &QCheckBox::toggled,
                this, &MainWindow::OnD3DRendererChanged);
        rendererLayout->addWidget(D3DRendererCheck);

        layout->addWidget(rendererGroup);
        layout->addStretch();

        return page;
    }

    void MainWindow::SetupTray()
    {
        Tray = new QSystemTrayIcon(this);

        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter painter(&pm);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor("#58a6ff"));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(4, 4, 24, 24);
        painter.setBrush(QColor("#0d1117"));
        painter.drawEllipse(10, 10, 12, 12);
        painter.end();

        Tray->setIcon(QIcon(pm));
        Tray->setToolTip("CSP Grayscale Viewer");

        auto* menu = new QMenu(this);
        menu->addAction("Show", this, [this]()
        {
            show();
            raise();
            activateWindow();
        });
        menu->addSeparator();
        menu->addAction("Quit", qApp, &QApplication::quit);

        Tray->setContextMenu(menu);
        connect(Tray, &QSystemTrayIcon::activated,
                this, &MainWindow::OnTrayActivated);

        Tray->show();
    }

    void MainWindow::SetupFocusSync()
    {
        FocusSyncTimer = new QTimer(this);
        FocusSyncTimer->setInterval(250);
        connect(FocusSyncTimer, &QTimer::timeout, this, [this]()
        {
            AppRef.ReevaluateFocus();
        });
        FocusSyncTimer->start();
    }

    void MainWindow::SetupLogSink()
    {
        for (const auto& line : csp::util::Logger::Instance().History())
        {
            AppendLogLine(QString::fromStdWString(line).trimmed());
        }

        csp::util::Logger::Instance().SetSink([this](const std::wstring& Line)
        {
            const QString text = QString::fromStdWString(Line).trimmed();
            QMetaObject::invokeMethod(this, [this, text]()
            {
                AppendLogLine(text);
            }, Qt::QueuedConnection);
        });
    }

    void MainWindow::AppendLogLine(const QString& Line)
    {
        if (!LogText || Line.isEmpty())
        {
            return;
        }

        LogText->appendPlainText(Line);
        auto* scrollBar = LogText->verticalScrollBar();
        if (scrollBar)
        {
            scrollBar->setValue(scrollBar->maximum());
        }
    }

    void MainWindow::OnNavClicked(int Index)
    {
        Stack->setCurrentIndex(Index);
        for (int i = 0; i < NavButtons.size(); ++i)
        {
            NavButtons[i]->setChecked(i == Index);
        }
    }

    void MainWindow::OnFpsChanged(int Value)
    {
        AppRef.SetRefreshRate(Value);
        SaveSettings();
    }

    void MainWindow::OnHotkeyChanged(const QKeySequence& Seq)
    {
        if (Seq.isEmpty())
        {
            return;
        }

        auto combo = Seq[0];
        UINT mods = 0;
        UINT vk = 0;

        auto qtMods = combo.keyboardModifiers();
        if (qtMods & Qt::ControlModifier)
        {
            mods |= MOD_CONTROL;
        }
        if (qtMods & Qt::AltModifier)
        {
            mods |= MOD_ALT;
        }
        if (qtMods & Qt::ShiftModifier)
        {
            mods |= MOD_SHIFT;
        }

        Qt::Key qtKey = combo.key();

        if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24)
        {
            vk = VK_F1 + (qtKey - Qt::Key_F1);
        }
        else if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
        {
            vk = 'A' + (qtKey - Qt::Key_A);
        }
        else if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
        {
            vk = '0' + (qtKey - Qt::Key_0);
        }
        else
        {
            return;
        }

        AppRef.SetHotkey(mods, vk);
        SaveSettings();
    }

    void MainWindow::OnFocusOnlyChanged(bool Checked)
    {
        AppRef.SetHotkeyFocusOnly(Checked);
        SaveSettings();
    }

    void MainWindow::OnAutoStartChanged(bool)
    {
        UpdateRegistryAutoStart();
        SaveSettings();
    }

    void MainWindow::OnStartMinimizedChanged(bool)
    {
        UpdateRegistryAutoStart();
        SaveSettings();
    }

    void MainWindow::OnD3DRendererChanged(bool Checked)
    {
        AppRef.SetD3DRendererEnabled(Checked);
        if (D3DRendererCheck->isChecked() != AppRef.IsD3DRendererEnabled())
        {
            D3DRendererCheck->blockSignals(true);
            D3DRendererCheck->setChecked(AppRef.IsD3DRendererEnabled());
            D3DRendererCheck->blockSignals(false);
        }
        SaveSettings();
    }

    void MainWindow::UpdateRegistryAutoStart()
    {
        QSettings reg("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
                      QSettings::NativeFormat);
        if (AutoStartCheck->isChecked())
        {
            QString path = QCoreApplication::applicationFilePath().replace('/', '\\');
            QString cmd = QString("\"%1\"").arg(path);
            if (StartMinCheck->isChecked())
            {
                cmd += " --minimized";
            }
            reg.setValue("CSP-Grayscale-Viewer", cmd);
        }
        else
        {
            reg.remove("CSP-Grayscale-Viewer");
        }
    }

    void MainWindow::OnAddApp()
    {
        std::set<QString> procs;
        DWORD pids[2048];
        DWORD needed;
        if (EnumProcesses(pids, sizeof(pids), &needed))
        {
            DWORD count = needed / sizeof(DWORD);
            for (DWORD i = 0; i < count; ++i)
            {
                HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
                if (!h)
                {
                    continue;
                }
                wchar_t path[MAX_PATH];
                DWORD sz = MAX_PATH;
                if (QueryFullProcessImageNameW(h, 0, path, &sz))
                {
                    QString full = QString::fromWCharArray(path);
                    QString name = full.mid(full.lastIndexOf('\\') + 1);
                    if (!name.isEmpty() && name.toLower() != "system" &&
                        !name.startsWith("svchost", Qt::CaseInsensitive))
                    {
                        procs.insert(name);
                    }
                }
                CloseHandle(h);
            }
        }

        QStringList items(procs.begin(), procs.end());
        items.sort(Qt::CaseInsensitive);

        bool ok;
        QString chosen = QInputDialog::getItem(this, "Add Target Process",
                                               "Select a running process or type an .exe name:",
                                               items, 0, true, &ok);

        if (ok && !chosen.isEmpty())
        {
            for (int i = 0; i < AppRefList->count(); ++i)
            {
                if (AppRefList->item(i)->text().compare(chosen, Qt::CaseInsensitive) == 0)
                {
                    return;
                }
            }
            AppRefList->addItem(chosen);
            SyncAppListToBackend();
            SaveSettings();
        }
    }

    void MainWindow::OnRemoveApp()
    {
        auto* item = AppRefList->currentItem();
        if (item)
        {
            delete AppRefList->takeItem(AppRefList->row(item));
            SyncAppListToBackend();
            SaveSettings();
        }
    }

    void MainWindow::OnTrayActivated(QSystemTrayIcon::ActivationReason Reason)
    {
        if (Reason == QSystemTrayIcon::DoubleClick ||
            Reason == QSystemTrayIcon::Trigger)
        {
            if (isVisible())
            {
                hide();
            }
            else
            {
                show();
                raise();
                activateWindow();
            }
        }
    }

    void MainWindow::UpdateStatusIndicator(bool Enabled, bool Active)
    {
        if (!Enabled)
        {
            StatusDot->setStyleSheet("background: #30363d; border-radius: 5px;");
            StatusLabel->setText("Inactive");
            StatusLabel->setStyleSheet("color: #8b949e; font-size: 12px;");
        }
        else if (Active)
        {
            StatusDot->setStyleSheet("background: #3fb950; border-radius: 5px;");
            StatusLabel->setText("Active");
            StatusLabel->setStyleSheet("color: #3fb950; font-size: 12px;");
        }
        else
        {
            StatusDot->setStyleSheet("background: #d29922; border-radius: 5px;");
            StatusLabel->setText("Waiting");
            StatusLabel->setStyleSheet("color: #d29922; font-size: 12px;");
        }
    }

    void MainWindow::LoadSettings()
    {
        int fps = Settings.value("General/RefreshRate", 60).toInt();
        FpsSpin->setValue(fps);

        QStringList apps = Settings.value("General/TargetApps",
                                            QStringList{"CLIPStudioPaint.exe"}).toStringList();
        AppRefList->addItems(apps);
        SyncAppListToBackend();

        bool focusOnly = Settings.value("Hotkey/FocusOnly", true).toBool();
        FocusOnlyCheck->setChecked(focusOnly);
        AppRef.SetHotkeyFocusOnly(focusOnly);

        bool autoStart = Settings.value("System/AutoStart", false).toBool();
        AutoStartCheck->blockSignals(true);
        AutoStartCheck->setChecked(autoStart);
        AutoStartCheck->blockSignals(false);

        bool startMin = Settings.value("System/StartMinimized", false).toBool();
        StartMinCheck->blockSignals(true);
        StartMinCheck->setChecked(startMin);
        StartMinCheck->blockSignals(false);

        bool useD3D = Settings.value("Renderer/UseD3D", false).toBool();
        D3DRendererCheck->blockSignals(true);
        D3DRendererCheck->setChecked(useD3D);
        D3DRendererCheck->blockSignals(false);
        AppRef.SetD3DRendererEnabled(useD3D);

        QString hkStr = Settings.value("Hotkey/Sequence", "F9").toString();
        QKeySequence seq(hkStr);
        HotkeyEdit->setKeySequence(seq);
        OnHotkeyChanged(seq);
    }

    void MainWindow::SyncAppListToBackend()
    {
        std::vector<std::wstring> targets;
        for (int i = 0; i < AppRefList->count(); ++i)
        {
            targets.push_back(AppRefList->item(i)->text().toStdWString());
        }
        AppRef.SetTargetApps(targets);
    }

    void MainWindow::SaveSettings()
    {
        Settings.setValue("General/RefreshRate", FpsSpin->value());

        QStringList apps;
        for (int i = 0; i < AppRefList->count(); ++i)
            apps << AppRefList->item(i)->text();
        Settings.setValue("General/TargetApps", apps);

        Settings.setValue("Hotkey/Sequence", HotkeyEdit->keySequence().toString());
        Settings.setValue("Hotkey/FocusOnly", FocusOnlyCheck->isChecked());
        Settings.setValue("System/AutoStart", AutoStartCheck->isChecked());
        Settings.setValue("System/StartMinimized", StartMinCheck->isChecked());
        Settings.setValue("Renderer/UseD3D", D3DRendererCheck->isChecked());

        Settings.sync();
    }

    void MainWindow::closeEvent(QCloseEvent* Event)
    {
        if (Tray && Tray->isVisible())
        {
            hide();
            Event->ignore();
        } else
        {
            Event->accept();
        }
    }
}
