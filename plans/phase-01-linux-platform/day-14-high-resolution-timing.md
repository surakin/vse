# Phase 1 / Day 14 — High-Resolution Timing

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 13 (process lifecycle & argv).

## Tasks

- [ ] Add a `clock_gettime(CLOCK_MONOTONIC, ...)` wrapper to Day 2's
  `os` layer (libc call, consistent with the resolved libc policy).
- [ ] Compute frame delta-time each main-loop iteration from
  consecutive timestamps.
- [ ] Implement a simple frame limiter (sleep/spin to a target frame
  time) so delta-time is stable enough to verify.
- [ ] Run the main loop for several seconds, logging delta-time each
  frame; confirm it stays stable within a small tolerance under the
  limiter.
- [ ] Commit.

**Done:** the main loop logs a stable delta-time under a frame limiter.
