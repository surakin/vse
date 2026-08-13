# Phase 2 / Day 34 — Stress & Throughput Test Suite

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Days 27-33 (the whole job system) and Phase 0 Day 15
(test harness).

## Tasks

- [ ] Write the randomized dependency-graph stress test: thousands of
  jobs with random fan-out/join shapes (built from Day 31's `Counter`),
  repeated under ThreadSanitizer, asserting no races/deadlocks and
  exactly-once execution per job.
- [ ] Write the work-stealing throughput test: submit heavily skewed
  load (all jobs queued on worker 0), verify other workers steal work,
  measure wall-clock time vs. a single-worker baseline.
- [ ] Write the nested `co_await` ordering test: a chain of 5 dependent
  async steps (each depending on the previous one's result), run 10,000
  times, confirming strict resolution order every time.
- [ ] Register all three with Phase 0 Day 15's test harness and wire
  them into `ctest`.
- [ ] Run `ctest`; confirm all three are green, and that the throughput
  test's log shows measured speedup scaling with core count (not just
  "didn't crash").
- [ ] Commit.

**Done:** `ctest` runs all three tests green, and the throughput test's
log shows measured speedup scaling with core count.
