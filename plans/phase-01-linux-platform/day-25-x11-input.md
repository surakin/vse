# Phase 1 / Day 25 — X11 Fallback: Input

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 24 (X11 connection & window).

## Tasks

- [ ] Handle `KeyPress`/`KeyRelease` via `Xutf8LookupString` (full
  keysym translation — not a minimal hand-rolled mapping), feeding the
  same engine key enum values Day 17's Wayland backend produces.
- [ ] Handle `ButtonPress`/`ButtonRelease`/`MotionNotify`, updating the
  same `InputState` struct Day 18 defined.
- [ ] Test key presses under at least two keyboard layouts, same as
  Day 17's Wayland test, and confirm matching engine key enum values
  for the same physical keys.
- [ ] Test mouse motion/buttons and confirm `InputState` matches the
  Wayland backend's behavior for equivalent interactions.
- [ ] Commit.

**Done:** key/mouse state under X11 matches the Wayland backend's
behavior for the same interactions.
