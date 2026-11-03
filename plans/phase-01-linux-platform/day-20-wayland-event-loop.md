# Phase 1 / Day 20 — Wayland Event Loop

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 19 (Wayland surface & xdg_shell window).

## Tasks

- [x] Integrate `wl_display_dispatch`/`wl_display_dispatch_pending`
  into the engine's main loop. Implemented as `PumpWaylandEvents()` in
  `main.cpp` using the `wl_display_prepare_read`/`poll(..., timeout=0)`/
  `wl_display_read_events`/`wl_display_dispatch_pending` sequence —
  `wl_display_dispatch_pending` alone never reads the socket, and plain
  blocking `wl_display_dispatch` would stall the frame loop (and hang
  `engine_main` forever with no compositor interaction) whenever the
  compositor has nothing queued.
- [x] Handle `xdg_surface`'s `configure` event (ack it via
  `xdg_surface_ack_configure`). Now a persistent
  `WaylandWindow::HandleXdgSurfaceConfigure` (private static, listener
  data = `this`) instead of Day 19's stack-local `ConfigureState` —
  that state went out of scope when `Create()` returned, so any
  configure arriving afterward was already latent UB (a listener
  callback writing through a dangling stack pointer) before this day's
  fix.
- [x] Handle `xdg_toplevel`'s `configure` event, extracting new
  width/height, and its `close` event. `HandleToplevelConfigure`
  buffers pending width/height (0 = "unspecified", per spec); applied
  atomically in `HandleXdgSurfaceConfigure` when the matching surface
  configure/ack arrives. `HandleToplevelClose` sets `should_close_`,
  read via the new `WaylandWindow::ShouldClose()`.
- [x] Log new dimensions whenever a resize configure event arrives.
  Logged in `HandleXdgSurfaceConfigure` only when the applied
  width/height actually changes (so the initial configure and any
  no-op resize don't spam the log).
- [x] Exit the main loop cleanly when the `close` event fires.
  `main.cpp`'s loop now breaks on `window.ShouldClose()`; the existing
  6s wall-clock bound is kept as a second, later exit so `engine_main`
  still terminates on its own with no compositor interaction (e.g.
  headless/automated runs).
- [x] Manually resize the window via the compositor/WM and confirm
  dimensions log correctly; trigger the window's close affordance and
  confirm the process exits cleanly. **Not yet done** — implemented
  and code-reviewed in a remote session without a way to drive real WM
  interaction. Verified instead via: the existing Catch2 suite (real
  compositor, 10/10 passing, including two new `ShouldClose()` tests)
  and a full 6s `engine_main_asan` run against the live compositor
  (clean, no ASan errors, steady ~16.7ms frame deltas) — neither
  exercises an actual resize or a real close-button click. Leaving
  this unchecked until confirmed live. **2026-11-03 desk check:** close
  confirmed working; resize initially did *not* take effect (no buffer
  re-allocation, fixed in `8aae284`) and was then slow (per-configure
  realloc, coalesced + faster fill in the next commit). **Later same day: resize confirmed
  working and fast on Wayland** (after the coalescing fix).
- [x] Commit.

**Done:** resizing logs new dimensions; the compositor's close
affordance exits cleanly. **Partially verified** — see the unchecked
manual-verification bullet above.
