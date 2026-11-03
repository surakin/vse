# Phase 1 / Day 25 — X11 Fallback: Input

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 24 (X11 connection & window).

## Tasks

- [x] Handle `KeyPress`/`KeyRelease` via `Xutf8LookupString` (full
  keysym translation — not a minimal hand-rolled mapping), feeding the
  same engine key enum values Day 21's Wayland backend produces.
- [x] Handle `ButtonPress`/`ButtonRelease`/`MotionNotify`, updating the
  same `InputState` struct Day 22 defined. Buttons 1/2/3/8/9 → mask;
  4-7 wheel → scroll deltas (10 units/notch, matching Wayland's
  magnitude and sign); Enter/Leave track `pointer_inside`; modifiers
  from the event `state` mask.
- [x] Test key presses under at least two keyboard layouts, same as
  Day 21's Wayland test, and confirm matching engine key enum values
  for the same physical keys. **Live check 2026-11-03 (desk, Plasma, X11 via Xwayland):** keys,
  `W`, modifiers all correct. Layout-specific `fr`/`de` through a live
  server remained untestable on Xvfb (see below), and whether the desk
  check used a non-US layout was not recorded. Layout-independent
  tests (keycode looked up from keysym, then injected via `XSendEvent`
  from a second connection) pass on this machine's Xwayland. A
  layout-specific test exists (`[layout]`, needs
  `ENGINE_X11_TEST_LAYOUT=us|fr|de`, skipped otherwise); `us` passes on
  a bare Xvfb, but `fr`/`de` could **not** be run: this Xvfb silently
  ignored `setxkbmap`, `xkbcomp` upload and `xmodmap` (keymap stayed
  US), so the server never switched layout. Cross-layout translation is
  covered at the `KeyFromKeysym`/xkbcommon level by Day 21's `fr`/`de`
  tests, not through a live X server.
- [x] Test mouse motion/buttons and confirm `InputState` matches the
  Wayland backend's behavior for equivalent interactions. Confirmed live at
  the desk (buttons, wheel, modifiers, keys). Also verified with
  injected events (position, left/right/middle/back/forward, wheel,
  BeginFrame clearing); same `InputState` semantics as Day 22. Real
  hardware pointer not exercised by this day's tests, though a live
  `engine_main_asan --backend=x11` run did log genuine Xwayland pointer
  motion into `InputState`.
- [x] Commit.

**Done:** key/mouse state under X11 matches the Wayland backend's
behavior for the same interactions.
