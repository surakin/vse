# Phase 1 / Day 26 — Backend Selection & Fallback Logic

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 25 (X11 fallback: input).

Defines the internal `Window`/input interface every later platform
backend plugs into — see this phase's design note (`PLAN.md`
assumption #11) on keeping its shape generic (room for touch/gamepad,
lifecycle events beyond open/resize/close) even though only
keyboard/mouse and desktop lifecycle are wired up here.

## Tasks

- [ ] Define the internal `Window` interface (open/close/resize/input
  callbacks or polling, per the design note above) that both backends
  implement.
- [ ] Refactor Day 14-23's Wayland code and Day 24-25's X11 code to
  each implement this one interface, so upper layers are
  backend-agnostic.
- [ ] Detect `$WAYLAND_DISPLAY` at startup; if unset or the connection
  fails, fall back to `$DISPLAY`/X11.
- [ ] Add an env var override for testing that forces one backend
  explicitly, bypassing auto-detection.
- [ ] Run the Day 23 "hello window" milestone under each backend forced
  explicitly via the override, confirming identical behavior.
- [ ] Commit.

**Done:** the Day-23 "hello window" milestone passes identically when
each backend is forced explicitly (env var override for testing).
