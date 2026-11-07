# Phase 12 / Day 151 — Win32 Window

**Phase:** [Phase 12 — Windows Port + DX12 Backend](../phase-12-windows-dx12.md)
**Depends on:** Day 150; `engine/foundation/os/window.h`, `engine/foundation/os/windows/FULLSCREEN.md`.

The `os::Window` backend for Windows (input is tomorrow). New files under `engine/foundation/os/windows/`.

## Tasks

- [ ] **Backend selection:** `WindowBackend::Win32` added; `ParseWindowBackend("win32")`; `PlanWindowBackends` on Windows yields Win32 for `Auto` and refuses
      Wayland/X11 with the reason; the Linux plan is unchanged (tests on both machines). The `window_backend` setting's enum names gain `win32`.
- [ ] **`Win32Window`** (`os/windows/win32_window.h/.cpp`): a window class registered once per process (`RegisterClassExW`, `CS_OWNDC` only if the spec needs it),
      `CreateWindowExW` with the client size the caller asked for (`AdjustWindowRectExForDpi`), title in UTF-8 converted, `WndProc` dispatching to the instance
      through `GWLP_USERDATA` (set in `WM_NCCREATE`); `PumpEvents` drains with `PeekMessageW` and never blocks; `ShouldClose` on `WM_CLOSE` (the window is not
      destroyed until the owner destroys it).
- [ ] **Size and state:** `Width`/`Height` in physical pixels from `WM_SIZE`; `IsMinimized` from `SIZE_MINIMIZED`; `ResizeSerial` bumped on every size change and on
      leaving minimize; the modal resize loop (`WM_ENTERSIZEMOVE`/`WM_EXITSIZEMOVE`) does not freeze the app's frame loop — the spec picks the approach (a timer
      during the loop, or rendering from `WM_PAINT`), and a test checks the serial changes during a programmatic resize.
- [ ] **DPI:** per-monitor-v2 (manifest from Day 141 plus `SetThreadDpiAwarenessContext` fallback); `WM_DPICHANGED` applies the suggested rectangle; the scale
      factor is exposed only if `window.h` gains it — decide whether this phase adds it (the UI phase needs it) and record.
- [ ] **Native info:** `GetNativeInfo` returns `NativeWindowKind::Win32` with `HINSTANCE` and `HWND`; `BackendName` is `"win32"`; `PlatformWindowHost` on Windows
      (`os/windows/platform_window_host.cpp`) mirrors the Linux one.
- [ ] **Fullscreen** per `FULLSCREEN.md`: borderless on the window's monitor, saved style and placement restored; `IsFullscreen` true once the `WM_SIZE` for the
      new rectangle arrives; `tests/foundation/window_fullscreen_test.cpp` runs on Windows.
- [ ] **Lifecycle events:** whatever `window.h` already models for suspend/resume/focus (`WM_ACTIVATEAPP`, `WM_POWERBROADCAST` `PBT_APMSUSPEND`/
      `PBT_APMRESUMEAUTOMATIC`); events the interface lacks are filed for the phase that adds them (`PLAN.md` assumption #11), not added ad hoc.
- [ ] **Window tests on Windows:** the shared window tests (`tests/foundation/window_*`) run in the `display` group, skipping through `SKIP_IF_NO_DISPLAY_SESSION()`
      on a locked session; Windows-specific cases (`win32_window_windows_test.cpp`, phase 12): DPI change by `SetWindowPos` across a simulated DPI, minimize and
      restore, close request, destroying the window with messages still queued, two windows at once.
- [ ] **Human check** (record here or file): a window appears, resizes by dragging, minimizes and restores, toggles fullscreen and closes with the close button.
- [ ] **Documentation:** `window.h` comments name Win32 where they list backends; `docs/guide/platform-windowing-and-input.md` "Windows"; the docs module map
      (`docs/reference/modules.txt`) includes the Windows window header if it is public, as the Linux ones are; reference regenerated; `docgen lint` zero.
- [ ] **Runs:** Windows `--affected` plain and asan; Linux `--affected` (`window.h` and the backend plan changed); quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: `WndProc` reentrancy (messages sent during `CreateWindowExW` and `DestroyWindow`, before
      `GWLP_USERDATA` is set and after the object is gone), the class unregistered while a window exists, the resize-loop strategy.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** a Win32 window opens, reports size, minimize, DPI and close correctly, toggles borderless fullscreen, hands its `HWND` to graphics code through
`NativeWindowInfo`, and the window tests pass on Windows.
