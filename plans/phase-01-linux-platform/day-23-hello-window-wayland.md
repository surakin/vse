# Phase 1 / Day 23 — "Hello Window" Milestone (Wayland)

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 14 (timing) and Days 18-22 (Wayland connection
through pointer input).

Ties the primary backend together into one working milestone.

## Tasks

- [ ] Wire connection/registry (Day 18), surface/window (Day 19), event
  loop (Day 20), keyboard (Day 21), and pointer (Day 22) into one
  program using Day 14's frame-timed main loop.
- [ ] Confirm the window opens and stays responsive (event loop keeps
  dispatching; no hangs) for an extended run.
- [ ] Confirm keyboard and mouse input update `InputState` while the
  window has focus.
- [ ] Confirm the compositor's close affordance exits the process
  cleanly (no hang, no crash, exit code 0).
- [ ] Commit.

**Done:** window opens, stays responsive, closes cleanly under Wayland.
