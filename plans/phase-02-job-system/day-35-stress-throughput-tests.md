# Phase 2 / Day 35 — Stress & Throughput Test Suite

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Days 27-34 (the whole job system) and Phase 0 Day 15
(test harness).

## Tasks

- [ ] Randomized dependency-graph stress test: thousands of jobs in
  random fan-out/join shapes built from `Counter` (seeded PRNG; the seed
  is printed and a failing seed can be replayed), each job bumping its
  own execution counter. Asserts exactly-once execution per job, no
  deadlock (a watchdog timeout fails the test with a diagnostic instead
  of hanging `ctest`), and frame/`FramePool` stats balanced at the end.
  Run under `jobs_tests_tsan`, and repeat enough times (e.g. 200 seeds in
  the normal build, fewer under TSan) to be meaningful.
- [ ] Work-stealing correctness+throughput test: submit heavily skewed
  load (every job spawned from one participant's deque). **Deterministic
  assertions:** per-participant executed-job counters show every
  participant other than the producer executed work, and total executed
  equals total submitted. **Timing:** CPU-bound jobs of fixed work
  (≥ ~50µs each so scheduling overhead doesn't dominate), best-of-5 wall
  time at `N = 1, 2, 4, default` (only configurations ≤ the allowed
  logical CPU count), logged as a table of speedup vs. `N = 1`.
- [ ] Define the speedup assertion so it is not flaky: assert speedup at
  `N` ≥ `0.5 × min(N, physical-parallelism available)` only when the
  allowed logical CPU count is ≥ 4 and the build is not a sanitizer
  build; otherwise log the table and skip the assertion (with `SKIP`, so
  ctest shows it was skipped rather than silently passed).
- [ ] Nested `co_await` ordering test: a chain of 5 dependent async
  steps (each step's job reads the previous step's output slot and may
  itself fan out and join), run 10,000 times, asserting strict order
  every time.
- [ ] Revisit `FramePool` size classes with real data: log the observed
  frame-size histogram from these tests and either confirm the Day 27
  classes or adjust them (record the numbers in the day file); fallback
  count must be zero in these runs.
- [ ] Register everything with the harness (normal `ctest`; the heavy
  TSan variants under the `tsan` label), and run `ctest` plus
  `ctest -L tsan`; all green.
- [ ] Commit.

**Done:** `ctest` (and the `tsan`-labelled set) is green for the
dependency-graph stress, skewed-load stealing, and 10,000-run nested
`co_await` ordering tests; the stealing test proves other participants
executed stolen work, and the logged speedup table shows scaling with
participant count (asserted on machines with ≥ 4 allowed CPUs outside
sanitizer builds).
