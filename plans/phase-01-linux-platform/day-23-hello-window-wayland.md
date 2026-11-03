# Phase 1 / Day 23 — "Hello Window" Milestone (Wayland)

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 14 (timing) and Days 18-22 (Wayland connection
through pointer input).

Ties the primary backend together into one working milestone.

## Tasks

- [x] Wire connection/registry (Day 18), surface/window (Day 19), event
  loop (Day 20), keyboard (Day 21), and pointer (Day 22) into one
  program using Day 14's frame-timed main loop. `main.cpp` now also
  inits `WaylandInput`, calls `InputState::BeginFrame()` before each
  event pump, and logs `input: ...` lines whenever the state changes
  (the Day 22 readout). New `--run-seconds=N` flag (default 6) sets the
  wall-clock bound for longer manual runs.
- [x] Confirm the window opens and stays responsive (event loop keeps
  dispatching; no hangs) for an extended run. 20s run on the live KDE
  compositor: 1196 frames, max delta 16.77ms, exit 0; 8s
  `engine_main_asan` run clean.
- [ ] Confirm keyboard and mouse input update `InputState` while the
  window has focus. **Not done** — needs a human at the keyboard/mouse
  (no input-injection tool available in this session). Run
  `engine_main --run-seconds=60` and watch the `input:` lines.
- [ ] Confirm the compositor's close affordance exits the process
  cleanly (no hang, no crash, exit code 0). **Not done** — same reason;
  the timed-out path exits 0 and the close path is wired
  (`ShouldClose()` → loop break → normal return), but no real close was
  triggered.
- [x] Commit.

**Done:** window opens, stays responsive, closes cleanly under Wayland.
**Partially verified** — see the unchecked manual items above.
