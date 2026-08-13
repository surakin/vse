# Phase 1 / Day 22 — Wayland Pointer Input

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 21 (Wayland keyboard input).

## Tasks

- [ ] Call `wl_seat_get_pointer` and install its listener (`enter`,
  `leave`, `motion`, `button`, `axis`).
- [ ] Define the engine's `InputState` struct: mouse position, button
  bitmask, scroll delta (plus room for keyboard state from Day 21).
- [ ] Update `InputState` from the `motion` (position), `button`
  (bitmask), and `axis` (scroll delta) callbacks.
- [ ] Manually move the mouse, click each button, and scroll over the
  window; verify `InputState` reflects each accurately via a debug log
  or on-screen readout.
- [ ] Commit.

**Done:** an `InputState` struct accurately reflects live mouse
position/buttons/scroll.
