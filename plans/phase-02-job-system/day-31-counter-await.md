# Phase 2 / Day 31 — Counter/Await Mechanism

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Day 30 (scheduler loop).

## Tasks

- [ ] Implement `Counter` in `/engine/jobs/counter.h` wrapping an
  atomic int, with `Increment()`/`Decrement()`.
- [ ] Implement `CounterAwaitable` with `await_ready()` (true if count
  == 0), `await_suspend(coroutine_handle<> h)` (registers `h` as the
  counter's continuation and returns to the scheduler without resuming
  `h`), and `await_resume()`.
- [ ] Wire `Counter::Decrement()` so that reaching zero reschedules the
  registered continuation onto the current worker's deque (or a fixed
  dispatch worker if decremented from a non-worker thread).
- [ ] Write a test: a parent job spawns 8 child jobs, each decrementing
  a shared `Counter` on completion; the parent does `co_await counter`
  after spawning them.
- [ ] Add an ordering assertion (a flag each child sets, checked by the
  parent only after it resumes past the `co_await`) and run it 10,000
  times, confirming no reordering.
- [ ] Commit.

**Done:** the parent job only proceeds past `co_await counter` after
all 8 children have completed, verified by the ordering assertion
across 10,000 repeated runs with no reordering.
