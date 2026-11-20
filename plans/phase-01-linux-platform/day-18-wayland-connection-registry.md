# Phase 1 / Day 18 — Wayland Connection & Registry

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Days 14-17 (timing, threading, file I/O, GPU driver
loading) — not a hard technical dependency, but this is where the
Wayland/X11 windowing work begins, sequenced after the rest of the
platform layer's non-windowing primitives are done.

## Tasks

- [x] Add `libwayland-client` (headers + link) to CMake.
- [x] Call `wl_display_connect(nullptr)`; confirm a non-null
  `wl_display*`, logging an error and exiting cleanly if it's null.
- [x] Call `wl_display_get_registry` and install a registry listener
  with `global`/`global_remove` callbacks.
- [x] In the `global` callback, bind `wl_compositor`, `xdg_wm_base`
  (via generated/vendored xdg-shell protocol bindings — decide
  `wayland-scanner`-generated vs. hand-written per this phase's Notes),
  `wl_seat`, and `wl_shm` when their interface names match.
- [x] Call `wl_display_roundtrip` so all globals are bound before
  proceeding.
- [x] Log each bound global's interface name and version.
- [x] Commit.

**Done:** registry globals for compositor/xdg_wm_base/seat/shm are
bound successfully and logged.

## Resolution

**xdg-shell bindings (the open decision this day's original notes
flagged):** generated once via `wayland-scanner` against the system
`xdg-shell.xml` (`wayland-protocols` package, stable protocol) and
committed under `engine/foundation/os/linux/wayland/generated/`
(`xdg-shell-client-protocol.h`, `xdg-shell-protocol.c`) — same pattern as
the Unicode data tables from Days 11-12. `wayland-scanner`/
`wayland-protocols` are dev-time-only as a result; only
`libwayland-client` (linked via `pkg_check_modules`, `PUBLIC` on
`foundation`/`foundation_asan`) is a real build/runtime dependency.
Provenance documented in that directory's `VENDORED.md`. Compiling the
generated `.c` file required adding `C` alongside `CXX` to the root
`project()` call, and scoping `ENGINE_PRODUCTION_COMPILE_OPTIONS`
(`-fno-exceptions`/`-fno-rtti`, C++-only flags) to CXX translation units
via `$<COMPILE_LANGUAGE:CXX>` so they don't get passed to the C compiler
for that one file.

**`WaylandConnection`** (new
`engine/foundation/os/linux/wayland/wayland_connection.h`/`.cpp`, in
`vse::os::wayland`, not exposed through `os.h` — no
portable interface exists yet; that's Day 26): `Connect()` wraps
`wl_display_connect`/`wl_display_get_registry`/a registry listener/
`wl_display_roundtrip`, returning an invalid (`Valid() == false`)
connection on any failure (no compositor, or a required global not
advertised) rather than crashing, move-only (owns the display/registry/
bound globals, cleans them up on destruction). Logs each bound global's
interface name and version via a new `LogPlatform` log channel
(`log/log.h`/`log/log.cpp`). Also installs an `xdg_wm_base` ping
listener that responds with `xdg_wm_base_pong` — one line beyond the
literal task list, but binding `xdg_wm_base` without it risks the
compositor treating the client as unresponsive per the xdg-shell spec,
so it's the minimal correct companion to that bind rather than scope
creep.

7 new tests in `tests/foundation/wayland_connection_test.cpp` (76 total,
all green) run against the real compositor present in this dev
environment (confirmed reachable via the default `wayland-0` socket even
with `$WAYLAND_DISPLAY` unset in the test shell), including one that
raises `LogPlatform`'s verbosity and captures the write sink to directly
verify the interface-name-and-version logging requirement, and one that
simulates no compositor (`$WAYLAND_DISPLAY` pointed at a nonexistent
socket) to verify the failure path doesn't crash. `engine_main_asan`
still runs clean.
