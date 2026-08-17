# Phase 1 / Day 19 — Wayland Surface & xdg_shell Window

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 18 (Wayland connection & registry).

## Tasks

- [x] Call `wl_compositor_create_surface` to get a `wl_surface*`.
- [x] Call `xdg_wm_base_get_xdg_surface` to wrap it as an
  `xdg_surface*`.
- [x] Call `xdg_surface_get_toplevel` to get an `xdg_toplevel*`; set a
  window title and app_id via the toplevel's requests.
- [x] Install the `xdg_surface` `configure` listener; call
  `xdg_surface_ack_configure` in response to acknowledge it.
- [x] Call `wl_surface_commit` to map the window.
- [x] Build and run against a real Wayland compositor (automated: 4 new
  tests create/map a real window against the compositor present in this
  dev environment; `engine_main`/`engine_main_asan` wired to create one
  too, ran clean under ASan/UBSan). **Human visual confirmation still
  pending** — this session ran over a remote connection with no way to
  look at the actual screen; see Resolution.
- [x] Commit.

**Done:** a real window appears on screen via a Wayland compositor.
Protocol-level/automated done condition met (see Resolution); the human
eyeball check is deferred to whenever the user is next at the physical
machine.

## Resolution

**Protocol gap flagged and fixed before implementation:** per xdg-shell,
a surface isn't actually mapped/visible until a buffer is attached and
committed *after* the first configure/ack_configure round-trip — the
literal task list (create surface → get xdg_surface → get toplevel →
commit) stops short of that and would leave the surface configured but
never shown. Confirmed with the user before writing code; added the
minimal `wl_shm` plumbing needed to complete the sequence (memfd-backed
shared memory, solid placeholder fill, `wl_shm_create_pool` +
`wl_shm_pool_create_buffer`, `wl_surface_attach` +
`wl_surface_damage_buffer` + a second `wl_surface_commit`). Real pixel
content is Phase 3's job (Vulkan swapchain); this fill exists only to
satisfy "a window appears" for this milestone.

**New `WaylandWindow`**
(`engine/foundation/os/linux/wayland/wayland_window.h`/`.cpp`, same
`engine_placeholder::os::wayland` namespace as Day 18's
`WaylandConnection`): `Create()` takes a `WaylandConnection&` plus
`StringView title`/`app_id`, blocks via `wl_display_roundtrip` until the
initial `xdg_surface` configure is received/acked, sizes the window from
the compositor's suggested `xdg_toplevel` size (falling back to a
1280x720 default when the compositor leaves it as "you choose"), and
maps it. Move-only, invalid-on-failure rather than crashing, same
pattern as `WaylandConnection`. Also installs an `xdg_toplevel`
`configure`/`close` listener — `close` is a no-op for now, explicitly
deferred to Day 20 (matches the pre-existing comment already in
`main.cpp` about that day replacing the fixed-duration loop with
"exit when the window's close event fires").

Window titles/app_ids are `StringView`, not `const char*` (same
filenames-are-UTF-8 reasoning as Days 16-17), but unlike file paths a
title has no OS-imposed length limit, so an overlong one is truncated
into a 256-byte stack buffer rather than treated as a hard failure —
cosmetic degradation, not a correctness problem the way a truncated path
would be.

**`main.cpp` wired up** (matching the Day 13/14 precedent of wiring
runnable days into the real entry point): connects and creates a window
before the existing frame-timed loop, non-fatally (logged via
`LogPlatform`, loop still runs even if Wayland is unavailable — same
"fail clearly, don't crash" pattern as Day 17's GPU probe). Loop
duration bumped from Day 14's 3s to 6s specifically so the window stays
up long enough to look at, and now pumps
`wl_display_dispatch_pending`/`wl_display_flush` each frame so the
connection (e.g. `xdg_wm_base` pings) stays responsive — a minimal
stand-in for Day 20's real event loop, not a replacement for it.

4 new tests in `tests/foundation/wayland_window_test.cpp` (80 total, all
green), including one that actually creates and maps a window against
the real compositor in this dev environment and asserts non-zero
width/height, one exercising the invalid-connection failure path, one
for title truncation, and one for move semantics. `engine_main_asan` run
live: builds, connects, maps the window, exits cleanly after 6s with no
ASan/UBSan errors — but this session is over a remote connection with no
way to actually look at the screen, so the literal "visually confirm" is
still an open item for the user to check locally.
