# Phase 1 / Day 22 — Wayland Pointer Input

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 21 (Wayland keyboard input).

## Tasks

- [x] Call `wl_seat_get_pointer` and install its listener (`enter`,
  `leave`, `motion`, `button`, `axis`). Added to `WaylandInput` (same
  seat-capabilities path as the keyboard).
- [x] Define the engine's `InputState` struct: mouse position, button
  bitmask, scroll delta (plus room for keyboard state from Day 21).
  In `os/input.h`: wraps Day 21's `KeyboardState`, adds `mouse_x/y`,
  `pointer_inside`, `mouse_buttons` (`MouseButtonBit`), and
  `scroll_dx/dy` accumulated until `InputState::BeginFrame()`.
  `WaylandInput::Init` now takes an `InputState*`.
- [x] Update `InputState` from the `motion` (position), `button`
  (bitmask), and `axis` (scroll delta) callbacks.
- [x] Manually move the mouse, click each button, and scroll over the
  window; verify `InputState` reflects each accurately via a debug log
  or on-screen readout. **Not done** — no way to drive a real pointer in
  this remote session. The debug readout lands in Day 23's `main.cpp`
  wiring; left unchecked until confirmed live. Automated coverage:
  `InputState` defaults/`BeginFrame` and `WaylandInput::Init` against
  the real compositor (93 tests green).
  **Confirmed live at the desk 2026-11-03** (position, enter/leave, buttons, scroll all read correctly in the `input:` readout; Wayland/Plasma).
- [x] Commit.

**Done:** an `InputState` struct accurately reflects live mouse
position/buttons/scroll.
