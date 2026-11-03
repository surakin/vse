# Phase 2 / Day 32 — Counter/Await Mechanism

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Day 31 (scheduler loop) and Day 28 (completion hook).

Phase design decision **D7**: single-waiter `Counter` with an atomic
continuation slot and an exactly-one-resumer protocol, and rescheduling
that works from any thread without a "fixed dispatch worker".

## Tasks

- [ ] Implement `Counter` in `/engine/jobs/counter.h`: an atomic count
  plus an atomic continuation slot (`coroutine_handle<>`, null =
  empty), with `Increment()` and `Decrement()`. Single awaiter at a time
  by contract: a second concurrent awaiter `ENGINE_ASSERT`s.
- [ ] Implement `CounterAwaitable`: `await_ready()` is true if the count
  is already zero; `await_suspend(h)` (returning `bool`) stores `h` into
  the slot, **then re-checks the count** — if it is zero, it tries to
  take the handle back with an atomic exchange on the slot; if it gets
  the handle it returns `false` (resume immediately, the decrementer
  never saw it), otherwise the decrementer owns the wake-up and it
  returns `true`; `await_resume()` is empty.
- [ ] `Decrement()`: when it takes the count to zero it atomically
  exchanges the slot with null and, if it got a handle, reschedules it —
  onto the current participant's deque if called from a participant,
  otherwise via the scheduler's injection queue (Day 31). The exchange on
  both sides guarantees exactly one of {decrementer, awaiter} resumes the
  coroutine — write this invariant in a comment above both functions.
- [ ] Write a test for **the race itself**: threads decrement the
  counter to zero at randomized moments around the awaiter's
  `await_ready`/`await_suspend`, 100,000 iterations, asserting the
  continuation runs exactly once every time (no lost wake-up, no double
  resume).
- [ ] Wire `Job::SetCompletion(Counter&)` (declared in Day 28) so a
  completing job decrements its counter from `final_suspend`; replace
  Day 28's test-only callback in its test with this.
- [ ] Add the `Counter` overload of `Scheduler::HelpUntil` (main waits
  by helping until the counter reaches zero).
- [ ] Test: a parent job `Increment`s a shared counter 8 times, submits 8
  child jobs bound to it via `SetCompletion`, then `co_await counter`;
  each child sets a flag, and the parent checks all 8 flags only after it
  resumes past the `co_await`. Run 10,000 times under the default pool
  and under `N = 1` and `N = 2`.
- [ ] Run the ordering test and the race test under `jobs_tests_tsan`
  and `jobs_tests_asan`.
- [ ] Commit.

**Done:** the parent proceeds past `co_await counter` only after all 8
children have completed — 10,000 runs per configuration, no reordering
— and the await/decrement race test shows exactly-once resumption over
100,000 iterations, clean under TSan.

- [ ] (from Day 28 review) The completion hook must not resume the waiting parent inline on the finishing job's stack: requeue it or use symmetric transfer, and test a deep continuation chain for stack growth.
