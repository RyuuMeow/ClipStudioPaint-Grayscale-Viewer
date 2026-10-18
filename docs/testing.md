# Development and verification

## Automated checks

Run from the repository root with Visual Studio 2022, Windows SDK and CMake installed:

```powershell
.\scripts\test.ps1 -Configuration Release -Smoke
.\scripts\test.ps1 -Configuration Debug -Smoke
```

The script configures the `windows` CMake preset, builds, runs CTest, and optionally launches the tray application with an isolated profile under `build/native/smoke`. The smoke process exits after 1.5 seconds. It checks startup and shutdown, not Explorer icon visibility or dialog interaction. CI runs these checks in both configurations without Qt installation or deployment.

Core regressions cover complete settings round trips (including 120 fps and empty targets), legacy Qt INI migration, malformed input without partial application, UTF-8 files at Unicode paths, atomic replacement, shortcut parsing, native `WM_HOTKEY` routing, conflicting replacement retention, suspension and focus-only unregistration, monitor geometry, and refresh limits. The shortcut routing test sends a message to its own control window; it does not synthesize a physical key press.

Renderer integration requires an **unlocked desktop and a DXGI-capable GPU**. It briefly displays a small colored test window without taking focus:

```powershell
.\scripts\test.ps1 -Configuration Release -Smoke -Renderers
```

This verifies transparent D3D startup, a completed first presentation, target resizing, 20 hide/show cycles per renderer, eight cancellations during first-frame preparation, no D3D presentations while hidden, five-second image-buffer release, reuse of the device after idle, cache invalidation on display changes, full shutdown, and stale Magnification timer messages. A missing GPU/capture service is a failure, not a silent pass. These checks do not establish pixel correctness, tablet input latency, or HDR fidelity.

## Idle measurements

```powershell
.\scripts\measure-idle.ps1 -Seconds 10
```

This launches an isolated Release profile with an empty target list, samples CPU over the specified interval after two seconds of warm-up, reports working set/private memory/executable size, and terminates only the process it started. CPU is expressed as a percentage of one logical core. A zero measurement means no CPU increment was observed during that interval; it is not proof of zero work under all conditions. Working set includes shared system DLL pages and is not interchangeable with private memory. Do not compare it with an active rendering workload or just the old executable size while excluding Qt DLLs.

Initial native-build verification on 2026-10-18 (Windows build 26300, MSVC 19.44, x64): Debug and Release passed the core suite, tray smoke test and both renderer integration tests. Before the custom icons and first-frame fix, a 10.01-second Release idle sample reported **14.30 MiB working set**, **2.74 MiB private memory**, **0.000% of one core** at the process timer's resolution, and a **420 KiB GUI executable**. Its import table contains Windows DLLs only. These are one-machine observations, not guarantees or a comparison with the old Qt build. Full menu interaction and actual CSP pen latency have not been manually verified. The installer was compiled, but installation was not exercised.

For meaningful latency/performance comparisons, use the same machine, window size, DPI, screen refresh rate, renderer and drawing workload. Measure idle, static preview, continuous drawing, resize/drag and focus changes separately. PresentMon/ETW and GPUView can examine frame pacing and GPU work; a high-speed camera or instrumented pen input is needed for end-to-end input-to-display latency.

## Architecture

- `tray/TrayApplication`: hidden broadcast window, notification icon, menu, startup registration and bounded diagnostics saved on request or normal exit. `SettingsDialogs` contains the two native dialogs.
- `core/Settings`: validated UTF-8 persistence and legacy import. Parsing has no UI callbacks and cannot overwrite another setting during initialization.
- `core/Application`: UI-thread state, native shortcut window, coalesced focus events, renderer choice and failure fallback. Inactive renderers are initialized only when needed.
- `d3d/D3DOverlayRenderer`: worker-owned HWND, D3D immediate context and COM resources. Atomic commands wake a waitable event; a one-shot timer paces active capture. Visibility/target revisions invalidate obsolete work. An inactive worker blocks without periodic ticks, releases image buffers after five seconds, and retains its device/shaders for fast resume.
- `magnification/MagnifierOverlay` and `MagnifierWorker`: compatibility renderer with worker-owned COM, Magnification API, windows, capture and painting. UI commands update a short locked request and wake an event; no Magnification call or paint runs under that lock. Revisions cancel obsolete startup/frames. Idle cleanup is asynchronous; full shutdown joins the worker. Hidden state guards queued timer messages; `SetTimer` is limited to a minimum interval of 10 ms and is stopped while hidden.
- `window/FocusTracker`: out-of-process foreground, location and minimization WinEvents. It does not inject code or hook a drawing application's rendering pipeline.

D3D keeps frames in GPU textures. `AcquireNextFrame(0)` does not block the UI; timeouts and cursor-only changes skip drawing unless a previous presentation needs retrying. `Present` uses `DXGI_PRESENT_DO_NOT_WAIT`. Capturing one output does not support a spanning window or rotated output in this implementation; those cases explicitly select Magnification. D3D requires Windows 10 2004 capture exclusion to prevent capturing its own overlay ([Microsoft API documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)).

### First-frame visibility

`Present` success means the frame was queued. The original reveal path made the host opaque before DWM consumed that frame; local diagnostics reproduced `submitted=1, composed=0` at the first reveal. The host now starts transparent, becomes minimally visible (alpha 1/255) before the first submission, and is made opaque only after the compositor reports that presentation completed. Alpha zero is unsuitable during preparation because DWM can skip the surface entirely. The worker uses `WaitForCommitCompletion`, `DwmFlush` and [DXGI frame statistics](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getframestatistics), checking cancellation between waits. The statistics wait is capped at 250 ms; unavailable statistics or a timeout select compatibility mode instead of revealing an unconfirmed frame. Normal frames do not use this synchronization path.

Local before/after instrumentation confirmed submitted and composed counts match before each successful reveal. The regression test also checks cancellation during the nearly transparent phase, so an obsolete first frame cannot make the window reappear after a toggle-off. Physical display appearance still needs confirmation on the affected user's display.

### Idle resume

Rebuilding the device after every five-second idle period caused a noticeable delay. The renderer now releases desktop duplication when hiding and releases capture textures, swap-chain buffers and composition objects after five seconds. It clears context bindings and calls [IDXGIDevice3::Trim](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgidevice3-trim) to discard unused driver allocations. The device, context, shaders and sampler stay cached; no capture, rendering or periodic timer continues while idle. This retains some driver/device memory instead of fully unloading it. Explicit suspension, switching away from D3D, failure and shutdown release the device; display changes invalidate it, and capture initialization also checks for device removal.

Run `scripts/test.ps1 -Configuration Release -ProfileRenderers` to print device, shader, composition and duplication setup timings plus initial/resume latency. On the same local machine, the instrumented baseline took **189.8 ms** to resume after idle, including **142.1 ms** for device creation. With the cache, the first comparison took **47.6 ms**, with no new device or shader creation. First-frame composition waits were similar (**26 ms** before, **27 ms** after), so the black-flash guard was retained. These are individual test-window samples, including up to one polling interval of observation delay; they are not physical display latency or guarantees for all drivers/window sizes. First activation after process launch still initializes the device on demand.

Final validation passed the core suite, renderer integration tests and tray startup smoke test in both Release and Debug, with no compiler warnings. Final idle-resume samples were **31.6 ms** (Release) and **48.4 ms** (Debug). The display-change test forces a replacement device and verifies successful presentation; shutdown verifies both the cache and image buffers are released. Portable and installer packages were rebuilt from the tested Release binaries.

### Embedded icons

The supplied SVG pack and unchanged PNG exports live in `resources/icons`. The full tile is embedded in the executable and used by settings dialogs and the installer; the transparent symbol is the notification icon. Multi-size ICO files are generated with `scripts/build-icons.ps1`, with no added runtime image library. See [icon sources](../resources/icons/README.md).

### Rapid physical shortcut investigation

A physical F9 reproduction log showed that the affected session used **Magnification fallback throughout**, despite preferring Direct3D. All 26 received shortcuts changed the requested state, but three waited **250, 63 and 94 ms** in the UI queue. The first activation spent about **284 ms** in the synchronous Magnification path. Received-event counts cannot establish how many physical presses Windows delivered. The earlier Direct3D-only integration test had missed this backend.

Running the same application integration against Magnification reproduced a **264.4 ms** maximum shortcut-handler duration. Moving Magnification initialization, capture, painting and cleanup to a worker reduced the first comparison to **8.1 ms**. These are local test-window measurements, not physical input-to-display latency. Magnification still has a three-update transparent warm-up; status now reports active only after this finishes. Its new activation measurement includes that preparation, so it cannot be compared directly with older measurements that reported active at alpha 1/255. Physical F9 behavior on the affected drawing window still requires user confirmation.

The application integration test now exercises both backends: 101 individual hotkey messages (including first activation), ten short-pause activations, and 100 bursts of 21 messages with final requested/rendered-state checks. A 100 ms handler ceiling catches the reproduced initialization stall. Tests include real updates to a temporary notification icon, automatic geometry fallback and return to Direct3D, worker window ownership, cancellation during Magnification warm-up, stale timer messages and asynchronous resource release/recreation. A foreground provider points only this Application instance at the test window; desktop focus is unchanged and no global keyboard events are injected. These checks cover routing after Windows delivers `WM_HOTKEY`, not physical keyboard delivery.

Diagnostics now record millisecond local timestamps, a sequence number for every received shortcut, registration changes, and preview states. The production message loop also records hotkeys that waited at least 50 ms in its queue. The last 1,000 lines are kept in memory and saved beside the active settings file as `diagnostics.log` on normal exit or `Open diagnostics`. The smoke check verifies automatic timestamped export on exit. To investigate an actual missed press, reproduce with the new build and use tray `Exit` promptly (or `Open diagnostics` while running), then compare `Shortcut received`, registration and `State` lines. No per-key disk writes or general keyboard monitoring are used.

Fallback diagnostics now separate unavailable display information, rotated displays, and windows extending beyond one output, and record target/output bounds, orientation, maximized state, the bounds source and display-query results. The user reports the affected window was wholly within one unrotated display. The older combined label cannot establish why it fell back; the new geometry log is needed before changing the capture policy.

Both Release and Debug passed core, tray smoke and renderer tests after the worker change. Final full-suite samples measured maximum Magnification shortcut-handler times of **6.0 ms** (Release) and **6.8 ms** (Debug); both backends passed all 100 bursts and automatic fallback/recovery passed. These timings remain local observations.

## Manual release checks

- Confirm left click, right click, keyboard navigation, both dialogs, physical F9 and modified shortcuts, conflicting bindings, and focus-only behavior in CSP.
- Check drawing with a pen/mouse, popups, Alt-Tab, minimize/restore, rapid toggles and renderer changes. Confirm correct grayscale pixels and no stale frame flash.
- Check monitor changes, negative coordinates, mixed DPI, rotated screens, spanning windows, multiple GPUs, sleep/resume, lock/unlock and Explorer restart.
- Check HDR and remote sessions separately. Protected content may remain unavailable even in compatibility mode.
- Check settings migration using a copy of the old file, startup registration, reinstall and portable relocation. Startup paths must be updated through the menu after moving the executable.

After testing Release, run `scripts/package.ps1` for the portable ZIP or add `-Installer` to also compile `installer.iss` with Inno Setup 6. The script and release workflow use an explicit GUI/console/license file list and cannot pick up leftover Qt deployment files. Existing Qt DLLs from an older installation may remain on disk after an in-place upgrade, but the native executable does not load them.
