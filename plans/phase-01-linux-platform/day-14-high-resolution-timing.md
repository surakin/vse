# Phase 1 / Day 14 — High-Resolution Timing

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 13 (process lifecycle & argv).

## Tasks

- [x] Add a `clock_gettime(CLOCK_MONOTONIC, ...)` wrapper to Day 2's
  `os` layer (libc call, consistent with the resolved libc policy).
  `os::MonotonicTimeNanos()` (nanoseconds, no defined epoch — deltas
  only) and `os::SleepNanos(u64)` (loops on `EINTR` using `nanosleep`'s
  remaining-time output) added to `os.h`/`os/linux/os.cpp`. TDD:
  `tests/foundation/os_timing_test.cpp` written first against the
  undeclared functions, confirmed RED via a real compile failure, then
  the implementation added for GREEN.
- [x] Compute frame delta-time each main-loop iteration from
  consecutive timestamps.
- [x] Implement a simple frame limiter (sleep/spin to a target frame
  time) so delta-time is stable enough to verify.
- [x] Run the main loop for several seconds, logging delta-time each
  frame; confirm it stays stable within a small tolerance under the
  limiter. `main.cpp` runs a ~60Hz-targeted loop (`kTargetFrameNanos` =
  16,666,667ns) bounded to ~3 seconds (`kRunDurationNanos`) so
  `engine_main` still exits on its own — Day 20 replaces this bound with
  "exit when the window's close event fires." Verified manually:
  delta-time settles to ~16.7-16.8ms per frame, within ~1% of target.
- [x] Commit.

**Done:** the main loop logs a stable delta-time under a frame limiter.
