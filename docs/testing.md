# Development and verification

## Automated checks

Run from the repository root with Visual Studio 2022, Windows SDK and CMake installed:

```powershell
.\scripts\test.ps1 -Configuration Release -Smoke
.\scripts\test.ps1 -Configuration Debug -Smoke
```

The script configures the `windows` CMake preset, builds, runs CTest, and optionally launches the tray application with an isolated profile under `build/native/smoke`. The smoke process exits after 1.5 seconds. It checks startup and shutdown, not Explorer icon visibility or dialog interaction. CI runs these checks in both configurations without Qt installation or deployment.

Core regressions cover complete settings round trips (including 120 fps and empty targets), legacy Qt INI migration, malformed input without partial application, UTF-8 files at Unicode paths, atomic replacement, shortcut parsing, native `WM_HOTKEY` routing, conflicting replacement retention, suspension and focus-only unregistration, monitor geometry, and refresh limits. The shortcut routing test sends a message to its own control window; it does not synthesize a physical key press.

Renderer integration requires an **unlocked desktop and a DXGI-capable GPU**. It displays a colored test window without taking focus, first small and then maximized with subtle animated updates:

```powershell
.\scripts\test.ps1 -Configuration Release -Smoke -Renderers
```

This verifies transparent D3D startup, a completed first presentation, target resizing, 20 hide/show cycles per renderer, eight cancellations around first-frame preparation, no D3D presentations while hidden, five-second image-buffer release, reuse of the device after idle, cache invalidation on display changes, full shutdown, and stale Magnification timer messages. It also samples a known-color test window during 24 reveals on each display and another 24 at maximized size. A missing GPU/capture service is a failure, not a silent pass. The pixel checks cover this SDR test pattern; they do not establish arbitrary-content accuracy, tablet input latency, or HDR fidelity.

## Idle measurements

```powershell
.\scripts\measure-idle.ps1 -Seconds 10
```

This launches an isolated Release profile with an empty target list, samples CPU over the specified interval after two seconds of warm-up, reports working set/private memory/executable size, and terminates only the process it started. CPU is expressed as a percentage of one logical core. A zero measurement means no CPU increment was observed during that interval; it is not proof of zero work under all conditions. Working set includes shared system DLL pages and is not interchangeable with private memory. Do not compare it with an active rendering workload or just the old executable size while excluding Qt DLLs.

Initial native-build verification on 2026-10-18 (Windows build 26300, MSVC 19.44, x64): Debug and Release passed the core suite, tray smoke test and both renderer integration tests. Before the custom icons and first-frame fix, a 10.01-second Release idle sample reported **14.30 MiB working set**, **2.74 MiB private memory**, **0.000% of one core** at the process timer's resolution, and a **420 KiB GUI executable**. Its import table contains Windows DLLs only. These are one-machine observations, not guarantees or a comparison with the old Qt build. Full menu interaction and actual CSP pen latency have not been manually verified. The installer was compiled, but installation was not exercised.

For meaningful latency/performance comparisons, use the same machine, window size, DPI, screen refresh rate, renderer and drawing workload. Measure idle, static preview, continuous drawing, resize/drag and focus changes separately. PresentMon/ETW and GPUView can examine frame pacing and GPU work; a high-speed camera or instrumented pen input is needed for end-to-end input-to-display latency.

## Architecture

- `tray/TrayApplication`: hidden broadcast window, notification icon, menu, startup registration and bounded diagnostics saved on request or normal exit. `SettingsDialogs` contains the two native dialogs.
- `tray/NotificationIcon`: serializes Shell icon registration, version selection, updates, recreation and removal on a worker. Requests keep only the latest tooltip; no Shell call holds the request lock. Failed registration retries after two seconds without a UI timer. Shutdown joins before the icon's HWND/HICON are destroyed.
- `core/Settings`: validated UTF-8 persistence and legacy import. Parsing has no UI callbacks and cannot overwrite another setting during initialization.
- `core/Application`: UI-thread state, native shortcut window, coalesced focus events, renderer choice and failure fallback. Inactive renderers are initialized only when needed.
- `d3d/D3DOverlayRenderer`: worker-owned HWND, D3D immediate context and COM resources. Atomic commands wake a waitable event; a one-shot timer paces active capture. Visibility/target revisions invalidate obsolete work. An inactive worker blocks without periodic ticks, releases image buffers after five seconds, and retains its device/shaders for fast resume.
- `magnification/MagnifierOverlay` and `MagnifierWorker`: compatibility renderer with worker-owned COM, Magnification API, windows, capture and painting. UI commands update a short locked request and wake an event; no Magnification call or paint runs under that lock. Revisions cancel obsolete startup/frames. Idle cleanup is asynchronous; full shutdown joins the worker. Hidden state guards queued timer messages; `SetTimer` is limited to a minimum interval of 10 ms and is stopped while hidden.
- `window/FocusTracker`: out-of-process foreground, location and minimization WinEvents. It does not inject code or hook a drawing application's rendering pipeline.

D3D keeps frames in GPU textures. `AcquireNextFrame(0)` does not block the UI; timeouts and cursor-only changes skip drawing unless a previous presentation needs retrying. `Present` uses `DXGI_PRESENT_DO_NOT_WAIT`. Capturing one output does not support a spanning window or rotated output in this implementation; those cases explicitly select Magnification. D3D requires Windows 10 2004 capture exclusion to prevent capturing its own overlay ([Microsoft API documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)).

### First-frame visibility

The first acquired duplication frame can contain **only a pointer update**, with `LastPresentTime=0` and `AccumulatedFrames=0`. The old path copied that resource whenever the crop was invalid or no frame had been captured yet. Local GPU readback reproduced an all-zero source texture, which the grayscale shader turned into opaque black. Waiting for successful presentation could not correct invalid image content. Cursor-only updates are now always released without copying; startup and resume wait for an actual desktop image. See [DXGI frame metadata](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_outdupl_frame_info).

`Present` success still means the frame was queued. The host starts transparent, becomes minimally visible (alpha 1/255) before the first valid submission, and becomes opaque after `WaitForCommitCompletion` and [DwmFlush](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmflush). The latter waits for the application's queued DirectX changes to be drawn. The worker checks cancellation before revealing. Alpha zero is unsuitable during preparation because DWM can skip the surface entirely. Normal frames retain the nonblocking presentation path.

The previous 250 ms gate compared DXGI presentation counters. It produced false compatibility fallbacks on the user's other display and intermittently during the pixel test. Microsoft documents that [these statistics are unreliable in many multiple-monitor scenarios](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getframestatistics); they no longer decide whether the first frame is ready. Actual composition errors still trigger fallback.

The pixel regression samples only one point inside a test-owned window and temporarily disables capture exclusion on its own overlay so the composed result is observable. With the constant `(220,60,100)` source, the baseline exposed black pixels; after the capture fix, the first comparison observed grayscale `(112,112,112)` and no dark samples over 24 reveals. Each cycle must produce visible grayscale and remain within the source's expected luminance range. Separate windows cover each display's DPI, including negative coordinates; a maximized case covers the reported 3440×1440 geometry. The test also keeps cancellation checks at varied points around acquisition/composition. Run the built `csp_renderer_tests.exe --reveal` for this focused check. Physical appearance in the user's drawing application remains a manual confirmation.

Final Release and Debug validation passed core regressions, both renderers, rapid hotkeys, idle/resource recovery and tray startup/diagnostic export. Across three displays (96/144/96 DPI) and the maximized case, the two builds completed **192 reveals and 3,011 pixel samples with no unexpected dark samples or fallback**. The maximized D3D shortcut handler stayed below 7 ms and the longest queued hotkey wait was 16 ms in both runs. Portable and installer packages were rebuilt from the tested Release executable.

### Idle resume

Rebuilding the device after every five-second idle period caused a noticeable delay. The renderer now releases desktop duplication when hiding and releases capture textures, swap-chain buffers and composition objects after five seconds. It clears context bindings and calls [IDXGIDevice3::Trim](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgidevice3-trim) to discard unused driver allocations. The device, context, shaders and sampler stay cached; no capture, rendering or periodic timer continues while idle. This retains some driver/device memory instead of fully unloading it. Explicit suspension, switching away from D3D, failure and shutdown release the device; display changes invalidate it, and capture initialization also checks for device removal.

Run `scripts/test.ps1 -Configuration Release -ProfileRenderers` to print device, shader, composition and duplication setup timings plus initial/resume latency. On the same local machine, the instrumented baseline took **189.8 ms** to resume after idle, including **142.1 ms** for device creation. With the cache, the first comparison took **47.6 ms**, with no new device or shader creation. First-frame composition waits were similar (**26 ms** before, **27 ms** after), so the black-flash guard was retained. These are individual test-window samples, including up to one polling interval of observation delay; they are not physical display latency or guarantees for all drivers/window sizes. First activation after process launch still initializes the device on demand.

Final validation passed the core suite, renderer integration tests and tray startup smoke test in both Release and Debug, with no compiler warnings. Final idle-resume samples were **31.6 ms** (Release) and **48.4 ms** (Debug). The display-change test forces a replacement device and verifies successful presentation; shutdown verifies both the cache and image buffers are released. Portable and installer packages were rebuilt from the tested Release binaries.

### Embedded icons

The supplied SVG pack and unchanged PNG exports live in `resources/icons`. The full tile is embedded in the executable and used by settings dialogs and the installer; the transparent symbol is the notification icon. Multi-size ICO files are generated with `scripts/build-icons.ps1`, with no added runtime image library. See [icon sources](../resources/icons/README.md).

### Rapid shortcuts and maximized windows

The affected user's log established a false compatibility fallback: a maximized window reported DWM bounds `(-8,-8)-(3448,1448)` on output `(0,0)-(3440,1440)`, with orientation zero and successful display queries. The containment check treated its hidden sizing border as another display. `window/WindowGeometry` now clips only maximized overflow within the window DPI's Windows sizing-border metrics. Renderer selection and both renderers use those same visible bounds. Restored windows, larger overflows and rotated outputs still retain the compatibility checks. Tests cover the exact reported geometry, scaled borders and negative output coordinates, and verify that the rendered host has no eight-pixel offset.

Moving Magnification to a worker fixed its synchronous initialization stall, but the small-window checks missed a second source of blocking. The next physical reproduction still showed **12 queue delays of 78–125 ms**. Repeating the integration with a **3440×1440 animated target** reproduced a **231.8 ms** shortcut-handler stall even on Direct3D. Instrumentation identified synchronous `Shell_NotifyIcon` updates repeatedly taking about **200–233 ms** on the UI thread. Icon operations now run on a worker and coalesce pending states, including registration/recreation and removal. A deliberately blocked fake Shell call verifies that UI requests return promptly, intermediate tooltips are discarded, and registration/version/update/delete ordering survives recreation and shutdown.

In the first large-window comparison after this fix, the maximum Direct3D handler duration was **4.7 ms**, mean short-pause activation was **19.0 ms**, and the longest queued hotkey wait was **16 ms**. The Shell call still took up to **250 ms** in the background. Magnification measured **2.1 ms** maximum handler time and **16 ms** queue wait with a **266 ms** background Shell call. Its approximately **80 ms** activation includes the three-update transparent warm-up; this is not comparable with older tests that reported active at alpha 1/255. All numbers are local observations, not physical keyboard-to-display latency guarantees.

Integration now exercises both backends at small and maximized sizes: 101 individual hotkey messages, ten delayed taps posted by a separate producer during capture, and 100 bursts of 21 messages with final requested/rendered-state checks. Handler and queued-message waits must stay below 100 ms. It uses the production icon worker with a temporary icon and waits for its final state to converge. Tests also retain automatic geometry fallback/recovery, transparent first frames, warm-up cancellation, stale timers, resource release and restart. A foreground provider targets only the test window; desktop focus is unchanged and no global keyboard events are injected. Physical F9 behavior in the actual drawing application still requires user confirmation.

Final Release and Debug runs passed core regressions, both sizes/backends, and tray startup/diagnostic-export smoke tests with no compiler warnings. The final maximized Direct3D handler maxima were **4.8 ms** and **4.7 ms**, respectively; both recorded a **16 ms** maximum hotkey queue wait. The portable ZIP and installer were rebuilt from the tested Release binaries. Installer execution and physical F9 remain manual checks.

Diagnostics record millisecond local timestamps, received shortcut sequence numbers, registration changes, preview states, slow UI dispatches and queue waits of at least 50 ms. Fallback lines include bounds, orientation, maximized state and display-query results; normalized maximum bounds are logged when selecting a renderer. Shutdown records the longest background Shell call. The last 1,000 lines are kept in memory and saved beside the active settings file as `diagnostics.log` on normal exit or `Open diagnostics`. The smoke check verifies timestamped export. Received-event counts alone cannot establish how many physical presses Windows delivered. No per-key disk writes or general keyboard monitoring are used.

## Manual release checks

- Confirm left click, right click, keyboard navigation, both dialogs, physical F9 and modified shortcuts, conflicting bindings, and focus-only behavior in CSP.
- Check drawing with a pen/mouse, popups, Alt-Tab, minimize/restore, rapid toggles and renderer changes. Confirm correct grayscale pixels and no stale frame flash.
- Check monitor changes, negative coordinates, mixed DPI, rotated screens, spanning windows, multiple GPUs, sleep/resume, lock/unlock and Explorer restart.
- Check HDR and remote sessions separately. Protected content may remain unavailable even in compatibility mode.
- Check settings migration using a copy of the old file, startup registration, reinstall and portable relocation. Startup paths must be updated through the menu after moving the executable.

After testing Release, run `scripts/package.ps1` for the portable ZIP or add `-Installer` to also compile `installer.iss` with Inno Setup 6. The script and release workflow use an explicit GUI/console/license file list and cannot pick up leftover Qt deployment files. Existing Qt DLLs from an older installation may remain on disk after an in-place upgrade, but the native executable does not load them.
