# Phase 2 / Day 29 — Work-Stealing Deque

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Day 28 (worker thread pool).

## Tasks

- [ ] Implement a lock-free Chase-Lev double-ended deque in
  `/engine/jobs/deque.h` holding `std::coroutine_handle<>`, backed by a
  fixed-capacity array (no dynamic resize — matches the
  no-malloc-by-default rule).
- [ ] Implement owner-side `PushBottom`/`PopBottom` (LIFO).
- [ ] Implement thief-side `StealTop` (FIFO) via compare-and-swap.
- [ ] Write a single-producer/single-consumer stress test: the owner
  thread pushes/pops on its own end while a second thread concurrently
  steals from the top, across 1M operations.
- [ ] Run the stress test under ThreadSanitizer; confirm zero races and
  that no handle is ever duplicated or dropped.
- [ ] Commit.

**Done:** the SPSC stress test (owner pushing/popping while a thief
steals concurrently) run under ThreadSanitizer shows zero races and the
deque never yields a duplicate or dropped handle across 1M operations.
