# Development and verification

## Current release

Version **1.1.0** is the native Windows tray release. User-facing changes and upgrade instructions are in the [changelog](../CHANGELOG.md). The application no longer depends on Qt; Direct3D and Magnification remain supported.

Local verification on 2026-10-20 covered Release and Debug core tests, both renderers, shortcut/focus recovery, idle recovery and startup/diagnostic export. The final pixel checks completed 192 reveals and 3,002 samples across three displays and a maximized window without unexpected pixels. An in-place installation upgrade preserved the settings file and startup path, and the installed executable matched the tested Release binary. The user subsequently reported noticeably smoother operation in CSP; this is qualitative feedback, not a measured pen-to-display latency result.

## Build and automated checks

Requires Windows 10/11 x64, Visual Studio 2022 with Desktop development with C++, a Windows SDK, and CMake 3.20+:

```powershell
.\scripts\test.ps1 -Configuration Release -Smoke
.\scripts\test.ps1 -Configuration Debug -Smoke
```

The script configures the `windows` CMake preset, builds, and runs CTest. `-Smoke` launches a separate settings profile under `build/native/smoke`, exits after 1.5 seconds, and checks diagnostic identity and timestamps. It does not inspect menu/dialog appearance. GitHub CI runs these checks for both configurations. CI and release jobs use `windows-2022` to match the Visual Studio 2022 generator; changing the runner requires checking its installed toolchain.

Core coverage includes settings round trips and migration, invalid-input handling, atomic UTF-8 saves, shortcut parsing/routing/conflicts, suspension, missed focus notifications, hide/show and minimize/restore, display geometry, refresh limits, and background notification-icon updates.

Desktop renderer checks require an **unlocked desktop and a DXGI-capable GPU**:

```powershell
.\scripts\test.ps1 -Configuration Release -Smoke -Renderers
```

These tests use their own windows and foreground provider; they do not change the user's actual focus or inject physical keys. They cover:

- Small and maximized windows with both renderers, rapid toggles, queued shortcut bursts and focus recovery.
- Transparent startup, valid first frames, cancellation, resize, automatic compatibility fallback and return to Direct3D.
- Hidden rendering stops, five-second image-buffer release, device reuse, display-change invalidation and shutdown.
- Pixel checks on each display and at maximized size, verifying ownership of the sampled point before every read.

A missing GPU/capture service fails the desktop suite. Focused renderer options are `--reveal`, `--reveal-maximized` and `--maximized`; `-ProfileRenderers` on the PowerShell script also prints setup and activation timings. Pixel coverage uses an SDR test pattern and does not establish HDR fidelity or arbitrary-content accuracy.

## Architecture and constraints

| Component | Responsibility |
| --- | --- |
| `tray/TrayApplication`, `SettingsDialogs` | Tray menu, on-demand dialogs, startup registration, single instance per profile, diagnostic export. |
| `tray/NotificationIcon` | Worker-owned Shell icon operations; coalesces pending status updates and handles Explorer restart. |
| `core/Settings` | Validated UTF-8 settings, atomic replacement and legacy migration. |
| `core/Application` | Shortcut registration, focus recovery, renderer selection and fallback. |
| `window/FocusTracker` | Foreground, location and minimize WinEvents, without injecting into drawing applications. |
| `d3d/D3DOverlayRenderer` | Worker-owned window, D3D context, GPU textures and composition. |
| `magnification/MagnifierOverlay`, `MagnifierWorker` | Worker-owned Magnification API, window, capture, painting and resource cleanup. |

WinEvents handle focus changes immediately. A 250 ms recovery check compares foreground/root handles and visibility/minimization. Stable target focus skips process lookup and renderer selection. Unresolved identification and shortcut conflicts retry once per second. Focus-only and suspension rules still apply.

Direct3D uses nonblocking capture and presentation. A pointer-only duplication update (`LastPresentTime=0`) is never copied as a new desktop image. The host begins transparent and is revealed only after a valid submission, composition commit and `DwmFlush`, with cancellation checked before reveal. Normal frames use `DXGI_PRESENT_DO_NOT_WAIT`. DXGI presentation statistics do not gate visibility because they can be unreliable with multiple displays. See Microsoft's [frame metadata](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_outdupl_frame_info), [composition flush](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmflush) and [frame statistics](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getframestatistics) documentation.

Hiding stops Direct3D rendering and releases desktop duplication. After five seconds, image buffers and composition objects are released and unused driver allocations are trimmed. The device and shaders stay cached for quick resume. Suspension, renderer changes, failure and shutdown release the cache; display changes invalidate it. This retains device memory while idle. The renderer has no idle capture/render timer; the application's focus recovery check remains active.

Capture bounds exclude only a maximized window's hidden sizing border, using Windows DPI-aware border metrics. Restored windows genuinely spanning outputs and rotated outputs still select Magnification. Direct3D requires Windows 10 version 2004 [capture exclusion](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity). Magnification's initialization, painting and cleanup also run off the shortcut thread, and its status becomes active after its transparent warm-up.

Icons are embedded at build time; see [icon sources](../resources/icons/README.md). No runtime image library is needed.

## Diagnostics and performance

`Open diagnostics` and normal exit save the last 1,000 in-memory records beside the active settings file as `diagnostics.log`. The header always identifies the executable path, compile time and settings path. Records include shortcut receipt, focus recovery, renderer state, fallback geometry, and UI dispatch/queue delays of at least 50 ms. There are no per-key disk writes or general keyboard hooks. Received-event counts do not prove how many physical key presses Windows delivered.

When comparing builds, exit the existing tray instance first. Opening another executable with the same settings profile reuses the running instance. Check the diagnostic header and Windows startup path if an older build appears to be running.

```powershell
.\scripts\measure-idle.ps1 -Seconds 10
```

The measurement script uses an isolated empty-target profile, samples CPU after warm-up, reports working set/private memory/executable size, and terminates only its own process. CPU is expressed as a percentage of one logical core. A zero sample is not proof of zero work; working set includes shared DLL pages. Compare matching hardware, DPI, refresh rates, window sizes and workloads. Shortcut-handler time and automated activation timing are not pen-to-display latency; that requires instrumented input or a high-speed camera.

## Packaging and release

```powershell
.\scripts\prepare-release.ps1 -Tag v1.1.0
.\scripts\package.ps1 -Installer
```

Preparation verifies agreement between the tag, CMake, executable resources, manifest and installer, then extracts the matching changelog entry. Packaging requires tested Release binaries and Inno Setup 6. It produces:

- `build/native/CSP-Grayscale-Viewer-Native-Portable.zip` with the GUI, optional console executable and license.
- `Output/CSP-Grayscale-Viewer_Setup.exe`.
- `build/native/SHA256SUMS.txt` for the generated packages.

The package file list is explicit, so old Qt DLLs cannot enter the ZIP. Old DLLs may remain after an in-place installation upgrade, but the native executable does not load them.

Merge the reviewed release change after CI passes, then tag that commit. The `Windows Release` tag workflow validates versions, rebuilds and tests Release, packages the binaries, and publishes the installer, ZIP, checksums and changelog notes. A manual workflow run saves the same assets without publishing a release. CI packages are rebuilt from the tag; their hashes can differ from local builds and must be checked against the checksums from that release.

## Manual checks and remaining coverage

- Tray/menu navigation, dialogs, physical shortcuts and focus changes in CSP and other target applications.
- Pen/mouse input, popups, rapid toggles, resize, Alt-Tab and minimize/restore.
- Mixed DPI, negative display coordinates, rotation, spanning windows, display changes, lock/unlock, sleep/resume and Explorer restart.
- HDR, protected content, remote sessions and multiple GPUs on the relevant hardware.
- Upgrade/migration, installation, portable relocation and Windows startup. After moving a portable executable, toggle startup off and on from its new location.
