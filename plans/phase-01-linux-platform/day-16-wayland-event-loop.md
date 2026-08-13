# Phase 1 / Day 16 — Wayland Event Loop

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 15 (Wayland surface & xdg_shell window).

## Tasks

- [ ] Integrate `wl_display_dispatch`/`wl_display_dispatch_pending`
  into the engine's main loop.
- [ ] Handle `xdg_surface`'s `configure` event (ack it via
  `xdg_surface_ack_configure`).
- [ ] Handle `xdg_toplevel`'s `configure` event, extracting new
  width/height, and its `close` event.
- [ ] Log new dimensions whenever a resize configure event arrives.
- [ ] Exit the main loop cleanly when the `close` event fires.
- [ ] Manually resize the window via the compositor/WM and confirm
  dimensions log correctly; trigger the window's close affordance and
  confirm the process exits cleanly.
- [ ] Commit.

**Done:** resizing logs new dimensions; the compositor's close
affordance exits cleanly.
