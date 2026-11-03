# Phase 2 / Day 31 — Scheduler Loop, Idle Parking & Main-Thread Help

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Day 30 (deque) and Day 29 (participants).

Phase design decisions **D4** (idle strategy), **D5** (injection queue),
**D6** (victim selection) and the "main helps only while waiting" half of
**D1** are implemented here. `Scheduler` is an ordinary object (not a
global); each participant thread keeps a thread-local pointer to the
scheduler it belongs to.

## Tasks

- [ ] `Scheduler` in `/engine/jobs/scheduler.h`/`.cpp`: owns the Day 29
  participants, one Day 30 deque per participant (including main's),
  and the injection queue below.
- [ ] Injection queue: a fixed-capacity (4096) mutex-guarded ring for
  work submitted by **non-participant threads** (and for deque
  overflow). Defined overflow policy: `ENGINE_ASSERT` with a clear
  message if both the caller's deque and the injection queue are full
  (a capacity bug, never silent loss).
- [ ] `Submit(Job&&)`: takes ownership (Day 28 rule); participant
  threads push to their own deque (spilling to the injection queue if
  full), non-participants go to the injection queue; then wake one
  parked worker if any.
- [ ] Worker loop per spawned participant: pop own deque (LIFO) → drain
  the injection queue → steal from a random victim chosen by a
  per-participant deterministic xorshift32 (seeded from the participant
  index), a bounded number of attempts per round → on a failed round,
  `yield()` backoff for a bounded number of rounds → then **park** on a
  `std::condition_variable`. Resume each acquired handle with
  `.resume()`.
- [ ] Lost-wakeup safety: the parked-worker count and the "work may
  exist" check are done under the same mutex/atomic protocol the
  submitter uses (write the protocol in a comment; a Submit racing a
  worker deciding to park must not strand work). Shutdown sets a flag,
  wakes all workers, and joins.
- [ ] `RunUntil(Counter&)`-style main help: define
  `Scheduler::HelpUntil(predicate-or-counter)` for participant 0 — run
  jobs from main's deque / injection queue / stealing (same acquire
  order as workers) until the condition is true, then return to the
  caller. Assert it is only called on participant 0 and **not from inside
  a running job** (jobs wait with `co_await`, never by blocking). The
  concrete `Counter` overload lands in Day 32; here use a plain
  `std::atomic<int>` predicate.
- [ ] Test: submit 10,000 trivial jobs (each increments a shared
  `std::atomic` and returns) from main, `HelpUntil` the atomic reaches
  10,000, assert exactly 10,000 and that frame stats balance.
- [ ] Test the configurations: `N = 1` (main alone), `N = 2`, and the
  default `N`.
- [ ] Test submission from a **non-participant** thread (plain
  `std::thread`) through the injection queue, plus a deque-overflow
  spill (submit more than the deque capacity from a participant).
- [ ] Test shutdown while workers are parked, and shutdown immediately
  after a burst (no hang, no leak, no lost job).
- [ ] Repeat the 10,000-job test 100 times per configuration, then under
  `jobs_tests_tsan` and `jobs_tests_asan`; counter exactly 10,000 every
  run.
- [ ] Commit.

**Done:** 10,000 trivial jobs complete with the counter exactly 10,000,
100 consecutive runs in each of `N = 1, 2, default`, with no flakiness;
non-participant submission, queue overflow spill, and parked-worker
shutdown all pass; clean under ASan and TSan.
