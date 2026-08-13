# Phase 2 / Day 30 — Scheduler Loop

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Day 29 (work-stealing deque).

## Tasks

- [ ] Implement each worker's main loop in `/engine/jobs/scheduler.h`/
  `.cpp`: pop from its own deque; if empty, attempt to steal from a
  random peer, with bounded retries and a brief backoff before trying
  again.
- [ ] Resume the popped/stolen `coroutine_handle` via `.resume()`.
- [ ] Implement a global shutdown flag checked each loop iteration for
  clean pool teardown (all workers exit their loop and join).
- [ ] Implement a `Submit(coroutine_handle<>)` entry point that pushes
  onto the calling thread's deque (or a fallback queue if called from
  a non-worker thread).
- [ ] Write a test submitting 10,000 trivial jobs (each incrementing a
  shared atomic counter and returning); wait for completion and check
  the counter.
- [ ] Run the test 100 times; confirm the counter is exactly 10,000
  every run, with no flakiness.
- [ ] Commit.

**Done:** submitting 10,000 trivial jobs to the pool completes and the
counter matches exactly 10,000, run 100 times with no flakiness.
