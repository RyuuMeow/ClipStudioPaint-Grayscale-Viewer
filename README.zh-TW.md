# CSP Grayscale Viewer

[English](README.md) | [繁體中文](README.zh-TW.md) | [简体中文](README.zh-CN.md) | [日本語](README.ja-JP.md)

Windows 原生系統匣灰階預覽工具，適用於 Clip Studio Paint 與其他繪圖軟體。預覽覆蓋層可穿透滑鼠與繪圖筆輸入，不會修改作品，也不會向繪圖程式注入程式碼。

## 使用方式

1. 從 [Releases](../../releases) 下載安裝版或可攜版，或依下方方式自行建置。
2. 執行 `CSP-Grayscale-Viewer.exe`，圖示會出現在 Windows 通知區域。
3. 切換到 Clip Studio Paint，按 **F9** 開關灰階預覽；左鍵點擊系統匣圖示也能切換。
4. 右鍵選單可設定目標程式、快捷鍵、更新頻率、渲染器、開機啟動，也可開啟診斷紀錄或離開。

目標程式與快捷鍵使用按需建立的原生對話框。預設僅在目標程式取得焦點時註冊快捷鍵；更換快捷鍵若發生衝突，會保留原本的綁定。支援 Win 修飾鍵，F12 為 Windows 保留鍵。

## 渲染方式

新設定預設使用 **Direct3D**，更新上限為 60 fps。擷取、灰階處理與呈現都在專用執行緒執行，不會向介面堆積逐幀訊息；沒有新畫面或僅游標變動時會略過不必要的重繪。隱藏後停止渲染，閒置五秒釋放畫面緩衝區，保留裝置與 shader 以加快重新開啟。焦點追蹤使用 Windows 事件，不再固定輪詢。

**Magnification** 保留為可手動選擇的相容模式。當 Direct3D 擷取失敗、視窗跨螢幕或螢幕旋轉時，會自動切換至相容模式；重新切回目標程式時可重試暫時性的 D3D 失敗。選單狀態會顯示目前模式。

最大化視窗位於螢幕外的隱藏邊框會排除於擷取範圍，不會因此誤判為跨螢幕。系統匣提示在背景更新並只保留最新狀態，避免 Windows 檔案總管的慢回應卡住快捷鍵。

Magnification 的初始化、擷取與閒置清理也由獨立執行緒處理，避免拖住快捷鍵。快速切換會取消過期的預覽請求；透明暖機結束後才顯示為啟用狀態。

- **Automatic** 依目標螢幕更新率設定上限；手動設定也不會超過螢幕更新率。
- Direct3D 上限為 144 fps；Magnification 受 Windows 計時器限制，上限為 100 fps。這些是上限，不代表保證達成的幀率。
- Direct3D 的[排除自身擷取功能](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)需要 Windows 10 2004 以上，較舊版本改用相容模式。
- HDR、受保護內容、遠端桌面、多顯示卡與實際繪圖筆延遲仍需在對應硬體驗證；目前沒有宣稱特定延遲改善數字。

## 設定與舊版遷移

設定會先驗證，再以原子替換方式寫入 `%APPDATA%\CSP-Grayscale-Viewer\native-settings.ini`。第一次啟動原生版時，會匯入同資料夾內的舊 `settings.ini`，保留原檔與既有渲染器偏好。啟動後固定常駐系統匣，取代舊設定視窗與啟動時最小化選項。

`Open diagnostics` 會將記憶體內有數量上限的紀錄匯出到設定檔旁；正常離開時也會存成 `diagnostics.log`，包含時間與收到的快捷鍵事件。不需要常駐日誌視窗或持續寫入磁碟。

使用獨立設定檔：

```powershell
.\CSP-Grayscale-Viewer.exe --settings .\test-settings.ini --configure
```

## 建置與測試

需要 Windows 10/11 x64、Visual Studio 2022 的「使用 C++ 的桌面開發」與 Windows SDK，以及 CMake 3.20 以上。不需要 Qt 或另外安裝 MSVC 執行階段。

```powershell
.\scripts\test.ps1 -Configuration Release -Smoke
```

產物位於 `build/native/Release`。GUI 執行檔可單獨攜帶使用；選用的 `_Console.exe` 使用預設設定並輸出主控台紀錄。只有製作安裝程式時才需要 Inno Setup 6。

Debug、桌面渲染整合測試與人工驗收項目見[測試及架構說明](docs/testing.md)。Windows CI 會執行 Debug／Release 核心回歸測試與系統匣啟動測試。

## 示範與授權

![灰階預覽示範](demo/demo.gif)

示範呈現灰階效果，其中的設定介面為原生系統匣改版前的舊版。

[MIT License](LICENSE)。
