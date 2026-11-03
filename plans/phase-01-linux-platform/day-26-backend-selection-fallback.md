# Phase 1 / Day 26 — Backend Selection & Fallback Logic

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 25 (X11 fallback: input).

Defines the internal `Window`/input interface every later platform
backend plugs into — see this phase's design note (`PLAN.md`
assumption #11) on keeping its shape generic (room for touch/gamepad,
lifecycle events beyond open/resize/close) even though only
keyboard/mouse and desktop lifecycle are wired up here.

## Tasks

- [x] Define the internal `Window` interface (open/close/resize/input
  callbacks or polling, per the design note above) that both backends
  implement. `os/window.h`: polled `Window` (Valid, PumpEvents,
  ShouldClose, Width/Height, BackendName); input flows into the
  `InputState` bound at open. Room for touch/gamepad (more `InputState`
  members) and extra lifecycle events (additive virtuals) is noted in
  the header. (`WindowBackend::Invalid`, not `None` — X11's `None` macro
  collides.)
- [x] Refactor Day 18-23's Wayland code and Day 24-25's X11 code to
  each implement this one interface, so upper layers are
  backend-agnostic. New `WaylandPlatformWindow` (connection + window +
  input + the event pump moved out of `main.cpp`); `X11Window` now
  derives from `Window`. `PlatformWindowHost` owns both by value and
  exposes the active one; `main.cpp` no longer names either backend.
- [x] Detect `$WAYLAND_DISPLAY` at startup; if unset or the connection
  fails, fall back to `$DISPLAY`/X11. `PlanWindowBackends` (pure,
  tested) + `PlatformWindowHost::Open`; tests cover unset env and a
  set-but-dead `WAYLAND_DISPLAY`.
- [x] Add an env var override for testing that forces one backend
  explicitly, bypassing auto-detection. `ENGINE_WINDOW_BACKEND=
  auto|wayland|x11` (unrecognized = error); the `--backend=` flag from
  Day 24 takes precedence. A forced backend never falls back.
- [x] Run the Day 23 "hello window" milestone under each backend forced
  explicitly via the override, confirming identical behavior. 10s
  runs, `engine_main` and `engine_main_asan`, each of wayland/x11/auto:
  all exit 0, 598 frames, max frame delta 16.8-17.8ms, no output other
  than the frame log. Input/close verified only via the automated tests
  (no human at the screen).
- [x] Commit.

**Done:** the Day-23 "hello window" milestone passes identically when
each backend is forced explicitly (env var override for testing). **Partially verified** — identical
automated behavior; live input/close-button checks still need a human.
