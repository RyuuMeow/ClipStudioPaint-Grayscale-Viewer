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

CSP Grayscale Viewer 是一款專為 Clip Studio Paint 與其他 Windows 繪圖軟體設計的灰階預覽輔助工具。它會在目標軟體上方建立可穿透滑鼠與筆輸入的灰階 overlay，讓你在不修改作品本身的情況下快速檢查明暗關係。

## 功能特色

- **目標視窗綁定**：只有在指定的目標應用程式，例如 `CLIPStudioPaint.exe`，取得焦點時才啟用灰階預覽。
- **全域快捷鍵**：可自訂快捷鍵切換灰階，預設為 `F9`。
- **Focus-only 快捷鍵模式**：只有目標軟體在前景時才註冊快捷鍵，避免干擾其他應用程式。
- **雙 renderer backend**：
  - **Magnification**：基於 Windows Magnification API 的穩定相容模式。
  - **Experimental Direct3D**：基於 GPU 的實驗性 renderer，提供更流暢的預覽與未來擴充基礎。
- **輸入穿透 overlay**：滑鼠與繪圖筆操作會穿透到下方繪圖軟體。
- **現代 Qt 托盤程式**：可最小化到系統托盤，在背景安靜運作。
- **Log 頁面**：可在程式內查看 focus、hotkey 與 renderer 診斷資訊。

## 運作原理

程式會追蹤目前前景視窗，並將 overlay 對齊到設定好的目標應用程式上方。Magnification 模式使用 Windows Magnification API 與灰階色彩矩陣。實驗性 Direct3D 模式則會擷取桌面輸出、裁切目標視窗範圍、透過 pixel shader 轉換灰階，最後以 DirectComposition overlay 呈現。

## 安裝

1. 到 [Releases](../../releases) 下載最新安裝檔。
2. 執行 `CSP-Grayscale-Viewer_Setup.exe`。
3. 啟動 CSP Grayscale Viewer，設定目標應用程式與快捷鍵。

## 使用方式

1. 開啟 Clip Studio Paint 或其他已設定的繪圖軟體。
2. 按下快捷鍵，預設為 `F9`，切換灰階預覽。
3. 從托盤開啟設定視窗，可調整刷新率、目標應用程式、快捷鍵、開機啟動與 renderer backend。
4. 若需要診斷 renderer 或焦點行為，可查看 Log 頁面。

## Renderer 說明

想要最高相容性時，建議使用 **Magnification** 模式。想要更流暢的顯示並願意測試新 backend 時，可使用 **Experimental Direct3D** 模式。

Direct3D backend 仍屬於實驗性功能。它也是未來加入更多 GPU 效果的基礎，例如對比檢查、gamma 預覽、LUT 與色覺模擬。

## 建置

### 需求

- Windows 10 / 11
- Visual Studio 2022 with MSVC
- CMake 3.20 或以上版本
- Qt 6.9.0 或以上版本
- Inno Setup 6，僅打包安裝檔時需要

### 指令

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/path/to/Qt/6.9.0/msvc2022_64"
cmake --build build --config Release --parallel

New-Item -ItemType Directory -Force deploy
Copy-Item build/Release/CSP-Grayscale-Viewer.exe deploy/
Copy-Item build/Release/CSP-Grayscale-Viewer_Console.exe deploy/
windeployqt --release --no-translations --no-opengl-sw deploy/CSP-Grayscale-Viewer.exe
```

## 授權

本專案採用 [MIT License](LICENSE) 授權。

Qt 以動態連結方式使用，遵循 LGPL 授權。使用者可依需求替換安裝目錄中的 Qt runtime DLL。
