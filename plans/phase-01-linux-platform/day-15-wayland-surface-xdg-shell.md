# Phase 1 / Day 15 — Wayland Surface & xdg_shell Window

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 14 (Wayland connection & registry).

## Tasks

- [ ] Call `wl_compositor_create_surface` to get a `wl_surface*`.
- [ ] Call `xdg_wm_base_get_xdg_surface` to wrap it as an
  `xdg_surface*`.
- [ ] Call `xdg_surface_get_toplevel` to get an `xdg_toplevel*`; set a
  window title and app_id via the toplevel's requests.
- [ ] Install the `xdg_surface` `configure` listener; call
  `xdg_surface_ack_configure` in response to acknowledge it.
- [ ] Call `wl_surface_commit` to map the window.
- [ ] Build and run against a real Wayland compositor; visually confirm
  a window appears.
- [ ] Commit.

**Done:** a real window appears on screen via a Wayland compositor.
