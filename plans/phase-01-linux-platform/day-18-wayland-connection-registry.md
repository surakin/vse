# Phase 1 / Day 18 — Wayland Connection & Registry

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Days 14-17 (timing, threading, file I/O, GPU driver
loading) — not a hard technical dependency, but this is where the
Wayland/X11 windowing work begins, sequenced after the rest of the
platform layer's non-windowing primitives are done.

## Tasks

- [ ] Add `libwayland-client` (headers + link) to CMake.
- [ ] Call `wl_display_connect(nullptr)`; confirm a non-null
  `wl_display*`, logging an error and exiting cleanly if it's null.
- [ ] Call `wl_display_get_registry` and install a registry listener
  with `global`/`global_remove` callbacks.
- [ ] In the `global` callback, bind `wl_compositor`, `xdg_wm_base`
  (via generated/vendored xdg-shell protocol bindings — decide
  `wayland-scanner`-generated vs. hand-written per this phase's Notes),
  `wl_seat`, and `wl_shm` when their interface names match.
- [ ] Call `wl_display_roundtrip` so all globals are bound before
  proceeding.
- [ ] Log each bound global's interface name and version.
- [ ] Commit.

**Done:** registry globals for compositor/xdg_wm_base/seat/shm are
bound successfully and logged.
