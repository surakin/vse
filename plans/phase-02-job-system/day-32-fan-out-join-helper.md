# Phase 2 / Day 32 — Fan-Out/Join Helper

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Day 31 (Counter/await mechanism).

## Tasks

- [ ] Implement `parallel_for(count, chunk_size, fn)` in
  `/engine/jobs/parallel_for.h`, built purely from Days 28-31's
  primitives: split `count` items into jobs of `chunk_size`, submit
  them all against one shared `Counter`, return a `Job` that
  `co_await`s that counter.
- [ ] Write a test: sum a 100,000-element array via `parallel_for`
  (each job sums its chunk into a per-chunk result, combined after the
  join).
- [ ] Compare the parallel result against a serial sum for exact
  equality.
- [ ] Run the test under multiple worker-count configurations (1, 2, N
  cores — e.g. via an env var overriding the pool size from Day 28) and
  confirm the result matches in every configuration.
- [ ] Commit.

**Done:** `parallel_for` over a 100,000-element array produces the same
total as a serial sum, run under multiple worker-count configurations
(1, 2, N cores).
