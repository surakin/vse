# Phase 2 / Day 30 — Work-Stealing Deque

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Day 29 (participants & pool) and Day 27's TSan target.

## Tasks

- [x] Implement a lock-free Chase-Lev deque in `/engine/jobs/deque.h`
  holding `std::coroutine_handle<>`, backed by a fixed-capacity
  power-of-two array (4096 slots per participant by default; no dynamic
  resize).
- [x] Define the full-deque policy: `PushBottom` returns `false` when
  full (never overwrites); the caller (Day 31's `Submit`) spills to the
  injection queue instead. `PopBottom`/`StealTop` return an explicit
  "empty" / "lost race" distinction so thieves can tell retry from
  nothing-to-steal.
- [x] Implement owner-side `PushBottom`/`PopBottom` (LIFO) and
  thief-side `StealTop` (FIFO via compare-and-swap on `top`), with
  documented memory orders. **TSan note:** ThreadSanitizer does not model
  standalone `std::atomic_thread_fence`, so write the algorithm with the
  ordering placed on the atomic operations themselves (seq_cst at the
  points where the published algorithm uses fences) rather than separate
  fences; correctness first, optimize only if Day 35 shows it matters.
- [x] Test (a): owner pushes/pops while **one** thief steals, 1M
  operations — every pushed handle is consumed exactly once (use unique
  non-null sentinel handles; tally by id).
- [x] Test (b): owner plus **four concurrent thieves** (the contended
  case Chase-Lev gets wrong most often, especially the last-element race
  between `PopBottom` and `StealTop`), 1M operations, same exactly-once
  tally. Also a targeted test hammering the single-element case.
- [x] Test (c): fill to capacity, confirm `PushBottom` reports full and
  nothing is lost or overwritten, then drain.
- [x] Run (a)-(c) under `jobs_tests_tsan`; zero reports. Also run under
  ASan.
- [x] Commit.

**Done:** with one thief and with four thieves, under ThreadSanitizer,
there are zero races and across 1M operations each pushed handle is
yielded exactly once (no duplicates, no drops); full-deque behavior is
defined and tested.

## Post-implementation code review (2026-11-03, `/code-review high`)

Fixed: the two relaxed `bottom_` restore stores in `PopBottom` are now
seq_cst (a plain store continues no release sequence in C++20, so a thief
reading it on weak-memory hardware could miss the slot write — matters for
the ARM/Apple ports); `PushBottom`'s `top_` load relaxed to acquire
(safe: stale top only makes the deque look fuller); redundant 4096-store
constructor loop removed and the "~34 KB, heap-allocate" note added;
tests now assert thieves really stole (`stolen > 0`, `steals > 0`),
count spurious/out-of-range handles (`g_bad_ids == 0`), and thieves
yield while empty instead of spinning.
Declined: a shared `kCacheLine` constant (no such constant exists yet;
revisit at the Apple-silicon port, Phase 6). `.vscode/` gitignore left
alone (not part of this change).
Verified: 1M ops with 1 and 4 thieves + 300k single-element rounds clean
under plain, ASan and TSan, 15 repeated plain runs and 5 TSan runs.
