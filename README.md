# CSP Grayscale Viewer

<p align="center">
  <img src="https://img.shields.io/badge/platform-Windows-blue">
  <img src="https://img.shields.io/badge/language-C++20-blue">
  <img src="https://img.shields.io/badge/framework-Qt%206.9-green">
  <img src="https://img.shields.io/github/license/RyuuMeow/ClipStudioPaint-Grayscale-Viewer">
</p>

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh-TW.md">繁體中文</a> |
  <a href="README.zh-CN.md">简体中文</a>
</p>

<p align="center">
  <img src="demo/demo.gif" alt="CSP Grayscale Viewer demo">
</p>

CSP Grayscale Viewer is a lightweight grayscale preview helper for Clip Studio Paint and other drawing applications on Windows. It overlays a click-through grayscale preview on top of the target app, so you can quickly check values without changing your artwork.

## Features

- **Target-aware overlay**: The grayscale preview is only active when a configured target application, such as `CLIPStudioPaint.exe`, is focused.
- **Global hotkey**: Toggle grayscale with a customizable shortcut. The default key is `F9`.
- **Focus-only hotkey mode**: Prevents the shortcut from being captured while you are working in other applications.
- **Two renderer backends**:
  - **Magnification**: Stable compatibility mode based on the Windows Magnification API.
  - **Experimental Direct3D**: GPU-based renderer for smoother preview and future effects.
- **Click-through overlay**: Mouse and pen input pass through to the drawing app.
- **Modern Qt tray app**: Runs quietly in the system tray with a small settings window.
- **Runtime log page**: Inspect focus, hotkey, and renderer diagnostics from inside the app.

## How It Works

The app tracks the foreground window and attaches an overlay to your configured target application. In Magnification mode, it uses the Windows Magnification API with a grayscale color matrix. In experimental Direct3D mode, it captures the desktop output, crops the target window area, applies a grayscale pixel shader, and presents the result through a DirectComposition overlay.

## Installation

1. Download the latest installer from [Releases](../../releases).
2. Run `CSP-Grayscale-Viewer_Setup.exe`.
3. Launch CSP Grayscale Viewer and configure your target applications and hotkey.

## Usage

1. Open Clip Studio Paint or another configured drawing app.
2. Press the configured hotkey, default `F9`, to toggle grayscale preview.
3. Open the tray app to adjust refresh rate, target applications, hotkey, startup behavior, and renderer backend.
4. Use the Log page when diagnosing renderer or focus behavior.

## Renderer Notes

Use **Magnification** mode when you want maximum compatibility. Use **Experimental Direct3D** mode when you want smoother rendering and are willing to test the newer backend.

The Direct3D backend is still experimental. It is intended as the foundation for future GPU effects such as contrast checks, gamma preview, LUTs, and color vision simulation.

## Build

### Requirements

- Windows 10 / 11
- Visual Studio 2022 with MSVC
- CMake 3.20 or later
- Qt 6.9.0 or later
- Inno Setup 6, only required for installer packaging

### Commands

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/path/to/Qt/6.9.0/msvc2022_64"
cmake --build build --config Release --parallel

New-Item -ItemType Directory -Force deploy
Copy-Item build/Release/CSP-Grayscale-Viewer.exe deploy/
Copy-Item build/Release/CSP-Grayscale-Viewer_Console.exe deploy/
windeployqt --release --no-translations --no-opengl-sw deploy/CSP-Grayscale-Viewer.exe
```

## License

This project is released under the [MIT License](LICENSE).

Qt is used through dynamic linking and is licensed under LGPL. Users may replace the Qt runtime DLLs in the installation directory when needed.
