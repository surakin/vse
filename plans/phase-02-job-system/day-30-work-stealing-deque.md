# Phase 2 / Day 30 — Work-Stealing Deque

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Day 29 (participants & pool) and Day 27's TSan target.

## Tasks

- [ ] Implement a lock-free Chase-Lev deque in `/engine/jobs/deque.h`
  holding `std::coroutine_handle<>`, backed by a fixed-capacity
  power-of-two array (4096 slots per participant by default; no dynamic
  resize).
- [ ] Define the full-deque policy: `PushBottom` returns `false` when
  full (never overwrites); the caller (Day 31's `Submit`) spills to the
  injection queue instead. `PopBottom`/`StealTop` return an explicit
  "empty" / "lost race" distinction so thieves can tell retry from
  nothing-to-steal.
- [ ] Implement owner-side `PushBottom`/`PopBottom` (LIFO) and
  thief-side `StealTop` (FIFO via compare-and-swap on `top`), with
  documented memory orders. **TSan note:** ThreadSanitizer does not model
  standalone `std::atomic_thread_fence`, so write the algorithm with the
  ordering placed on the atomic operations themselves (seq_cst at the
  points where the published algorithm uses fences) rather than separate
  fences; correctness first, optimize only if Day 35 shows it matters.
- [ ] Test (a): owner pushes/pops while **one** thief steals, 1M
  operations — every pushed handle is consumed exactly once (use unique
  non-null sentinel handles; tally by id).
- [ ] Test (b): owner plus **four concurrent thieves** (the contended
  case Chase-Lev gets wrong most often, especially the last-element race
  between `PopBottom` and `StealTop`), 1M operations, same exactly-once
  tally. Also a targeted test hammering the single-element case.
- [ ] Test (c): fill to capacity, confirm `PushBottom` reports full and
  nothing is lost or overwritten, then drain.
- [ ] Run (a)-(c) under `jobs_tests_tsan`; zero reports. Also run under
  ASan.
- [ ] Commit.

**Done:** with one thief and with four thieves, under ThreadSanitizer,
there are zero races and across 1M operations each pushed handle is
yielded exactly once (no duplicates, no drops); full-deque behavior is
defined and tested.
