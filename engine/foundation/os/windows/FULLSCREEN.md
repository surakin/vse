# Fullscreen on Windows (checklist for Phase 11)

`os::Window::SupportsFullscreen`, `SetFullscreen` and `IsFullscreen` are the interface (`engine/foundation/os/window.h`).

- **Borderless fullscreen (the default):** save `GetWindowLongPtr(GWL_STYLE)` and `GetWindowPlacement`, remove `WS_OVERLAPPEDWINDOW`,
  `SetWindowPos` to the monitor rectangle of `MonitorFromWindow(MONITOR_DEFAULTTONEAREST)` / `GetMonitorInfo` with
  `HWND_TOP | SWP_FRAMECHANGED`; restore the saved style and placement to leave. This works with a flip-model swap chain and
  needs no mode change.
- **Exclusive fullscreen** is not wanted: DXGI's `SetFullscreenState` is discouraged on flip-model; do not implement it unless a
  measurement shows a latency gain. If it is added, it must be a separate request, not the `fullscreen` setting.
- `IsFullscreen` is the stored state, true once the `WM_SIZE` for the new rectangle arrived (that also bumps `ResizeSerial`).
- DPI: use the monitor's rectangle in physical pixels (per-monitor-v2 awareness).
- `SupportsFullscreen` is true for any valid top-level window.
- Test: the same toggle test as Linux (`tests/foundation/window_fullscreen_test.cpp`) in the `display` resource group.
