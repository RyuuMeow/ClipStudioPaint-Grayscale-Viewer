# CSP Grayscale Viewer

[English](README.md) | [繁體中文](README.zh-TW.md) | [简体中文](README.zh-CN.md) | [日本語](README.ja-JP.md)

Windows 原生托盘灰度预览工具，适用于 Clip Studio Paint 和其他绘图软件。覆盖层允许鼠标与绘图笔输入穿透，不会修改作品，也不会向绘图软件注入代码。

## 使用

1. 从 [Releases](../../releases) 下载安装版或便携版，或自行构建。
2. 运行 `CSP-Grayscale-Viewer.exe`，图标会出现在 Windows 通知区域。
3. 切换到 Clip Studio Paint，按 **F9** 开关预览；左键点击托盘图标也可切换。
4. 右键菜单可设置目标程序、快捷键、刷新上限、渲染器、开机启动，以及打开诊断或退出。

目标程序和快捷键使用按需创建的原生对话框。默认仅在目标程序获得焦点时注册快捷键。新快捷键冲突时保留原绑定；支持 Win 修饰键，F12 为 Windows 保留键。

## 渲染

新配置默认使用 **Direct3D**，刷新上限为 60 fps。捕获、灰度转换和呈现由专用线程处理，不向界面堆积逐帧消息；没有新画面或仅光标变化时跳过不必要的重绘。隐藏后停止渲染，空闲五秒释放 GPU 资源。焦点跟踪采用 Windows 事件，不再定期轮询。

**Magnification** 保留为可手动选择的兼容模式。Direct3D 捕获失败、窗口跨屏或显示器旋转时自动回退；重新切回目标应用时可重试暂时性的 D3D 故障。菜单状态显示当前模式。

- **Automatic** 跟随目标显示器刷新率，手动设置也会限制在显示器刷新率以内。
- Direct3D 上限 144 fps；Magnification 受 Windows 定时器限制，上限 100 fps。这些是上限，并非保证达到的帧率。
- Direct3D 的[覆盖层捕获排除功能](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)需要 Windows 10 2004 以上，旧版本使用兼容模式。
- HDR、受保护内容、远程桌面、多显卡及实际绘图笔延迟仍需在相应硬件验证；目前不宣称具体延迟改善数值。

## 设置迁移

配置先验证，再通过原子替换保存到 `%APPDATA%\CSP-Grayscale-Viewer\native-settings.ini`。首次启动原生版会导入同目录的旧 `settings.ini`，保留原文件及原渲染器偏好。程序启动后直接驻留托盘，取代旧设置窗口和启动时最小化选项。

`Open diagnostics` 按需将有数量上限的内存日志导出到配置文件旁，不需要常驻日志窗口或持续写入磁盘。

```powershell
.\CSP-Grayscale-Viewer.exe --settings .\test-settings.ini --configure
```

以上命令使用独立配置文件。

## 构建和测试

需要 Windows 10/11 x64、Visual Studio 2022 的 C++ 桌面开发组件和 Windows SDK，以及 CMake 3.20 以上。不需要 Qt 或单独安装 MSVC 运行时。

```powershell
.\scripts\test.ps1 -Configuration Release -Smoke
```

输出位于 `build/native/Release`。GUI 可执行文件可独立使用；可选的 `_Console.exe` 使用默认配置并输出控制台日志。仅打包安装程序时需要 Inno Setup 6。

Debug、桌面渲染集成测试和手动检查见[测试及架构说明](docs/testing.md)。Windows CI 执行 Debug／Release 核心回归测试和托盘启动测试。

## 演示和许可

![灰度预览演示](demo/demo.gif)

演示中的设置界面属于原生托盘改版前的版本。

[MIT License](LICENSE)。
