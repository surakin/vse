# Phase 1 / Day 23 — "Hello Window" Milestone (Wayland)

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Days 14-19 (Wayland connection through timing).

Ties the primary backend together into one working milestone.

## Tasks

- [ ] Wire connection/registry (Day 14), surface/window (Day 15), event
  loop (Day 16), keyboard (Day 17), and pointer (Day 18) into one
  program using Day 19's frame-timed main loop.
- [ ] Confirm the window opens and stays responsive (event loop keeps
  dispatching; no hangs) for an extended run.
- [ ] Confirm keyboard and mouse input update `InputState` while the
  window has focus.
- [ ] Confirm the compositor's close affordance exits the process
  cleanly (no hang, no crash, exit code 0).
- [ ] Commit.

**Done:** window opens, stays responsive, closes cleanly under Wayland.
