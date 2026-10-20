# Changelog

## 1.1.0 - 2026-10-20

### Native tray application

- Replace the Qt settings window with a native Windows tray menu and on-demand settings dialogs. Qt and a separate MSVC runtime installation are no longer required.
- Add refreshed application and tray icons, validated settings, atomic saves, and automatic import of existing settings.
- Keep Magnification available as a selectable compatibility renderer. New profiles default to Direct3D at a 60 fps limit; existing renderer preferences are preserved.

### Responsiveness and reliability

- Run capture, rendering, Magnification initialization and notification-icon updates outside the shortcut thread.
- Reuse the Direct3D device after idle while releasing image buffers and stopping hidden rendering.
- Wait for a valid desktop image before revealing the overlay, fixing black frames during activation.
- Exclude invisible maximized-window borders from capture bounds, preventing false cross-monitor compatibility fallbacks.
- Recover target-only shortcuts after missed focus notifications, temporary null foreground windows, minimize/restore and released shortcut conflicts.
- Save timestamped shortcut and renderer diagnostics with the executable path, build time and settings path.

### Upgrading

1. Exit the existing viewer from its tray menu before replacing a portable build or installing the update.
2. Install `CSP-Grayscale-Viewer_Setup.exe`, or extract `CSP-Grayscale-Viewer-Native-Portable.zip` and run the GUI executable.
3. Existing settings are retained. The first native launch imports the old `settings.ini` without modifying it.

Only one instance runs per settings profile. Opening another copy while the old one is running uses the existing instance. If you move a portable executable, turn **Start with Windows** off and on from the new location to update its startup path.

### Compatibility and downloads

- Windows 10/11 x64. Direct3D requires Windows 10 version 2004 or later; older versions use Magnification.
- Spanning windows, rotated displays and capture failures use Magnification automatically.
- HDR, protected content, remote sessions and multiple-GPU configurations still need environment-specific validation.
- Release assets include the installer, portable ZIP and `SHA256SUMS.txt`. Use `Get-FileHash -Algorithm SHA256` to compare a download with the published checksum.

See [development and verification](https://github.com/RyuuMeow/ClipStudioPaint-Grayscale-Viewer/blob/main/docs/testing.md) for automated coverage and remaining manual checks.
