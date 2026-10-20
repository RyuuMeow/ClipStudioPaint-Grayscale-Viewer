# CSP Grayscale Viewer

[English](README.md) | [繁體中文](README.zh-TW.md) | [简体中文](README.zh-CN.md) | [日本語](README.ja-JP.md)

A native Windows tray utility for checking grayscale values in Clip Studio Paint and other drawing apps. The click-through preview does not change your artwork or inject code into the drawing app.

**Version 1.1.0** · [Changelog](CHANGELOG.md)

## Use

1. Download an installer or portable build from [Releases](../../releases), or build the source below.
2. Run `CSP-Grayscale-Viewer.exe`. It appears in the Windows notification area.
3. Focus Clip Studio Paint and press **F9** to toggle the preview. Left-clicking the tray icon also toggles it.
4. Right-click the icon to choose target applications, set a shortcut, change the refresh limit or renderer, enable Windows startup, open diagnostics, or exit.

Shortcut and target editing use small native dialogs created only when opened. The shortcut is registered only while a target is focused by default; a conflicting replacement keeps the previous binding. The Win modifier is supported, and F12 is reserved by Windows.

## Rendering

New profiles use **Direct3D** at a 60 fps limit. Capture and tray updates run in the background so they do not block shortcuts. Hidden previews stop rendering, and cached graphics resources make reopening faster. Focus recovery restores shortcuts if a focus notification is missed.

**Magnification** remains available as a selectable compatibility mode. Direct3D falls back to it for capture failures, windows spanning displays and rotated displays. Invisible maximized-window borders do not trigger a false fallback. The tray menu identifies the active mode.

- **Automatic** follows the target display refresh rate. Manual limits are also capped to that rate: up to 144 fps for Direct3D and 100 fps for Magnification. These are limits, not guaranteed frame rates.
- Direct3D requires Windows 10 version 2004 or later; older versions use compatibility mode.
- HDR, protected content, remote sessions, multiple GPUs and physical pen latency still need environment-specific validation.

## Settings and migration

Before upgrading, exit the running viewer from its tray menu. Install the update over the existing installation, or replace the portable files. Only one instance runs per settings profile, so opening a new copy while an old one is running reuses that old instance. After moving a portable executable, turn **Start with Windows** off and on from the new location.

Settings are validated before use and saved by atomic file replacement to `%APPDATA%\CSP-Grayscale-Viewer\native-settings.ini`. On first native launch, an existing `settings.ini` in that folder is imported without modifying the original. Existing renderer preferences are retained. The old settings window and its start-minimized option are replaced by tray-only startup.

`Open diagnostics` exports the bounded in-memory log beside the settings file. Normal exit also saves it as `diagnostics.log`, including the executable path, build time, settings path, timestamps and received shortcut events. No live log window or continuous log-file writes are needed.

For an isolated profile:

```powershell
.\CSP-Grayscale-Viewer.exe --settings .\test-settings.ini --configure
```

## Build and test

Requires Windows 10/11 x64, Visual Studio 2022 with Desktop development with C++ and a Windows SDK, and CMake 3.20+. Qt and a separately installed MSVC runtime are not required.

```powershell
.\scripts\test.ps1 -Configuration Release -Smoke
```

Outputs are in `build/native/Release`. The GUI executable is portable on its own; the optional `_Console.exe` runs with default settings and console diagnostics. Inno Setup 6 is only needed to package the installer.

See [testing and architecture notes](docs/testing.md) for Debug, desktop renderer tests, and manual checks. Windows CI runs the core regressions and tray smoke test for Debug and Release.

## Demo and license

Native system tray menu in v1.1.0:

![Native system tray menu](demo/tray-menu.png)

![Grayscale preview demo](demo/demo.gif)

The demo shows the preview effect; its settings UI predates the native tray version.

[MIT License](LICENSE).
