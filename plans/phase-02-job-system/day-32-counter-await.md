# Phase 2 / Day 32 — Counter/Await Mechanism

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Day 31 (scheduler loop) and Day 28 (completion hook).

Phase design decision **D7**: single-waiter `Counter` with an atomic
continuation slot and an exactly-one-resumer protocol, and rescheduling
that works from any thread without a "fixed dispatch worker".

## Tasks

- [x] Implement `Counter` in `/engine/jobs/counter.h`: an atomic count
  plus an atomic continuation slot (`coroutine_handle<>`, null =
  empty), with `Increment()` and `Decrement()`. Single awaiter at a time
  by contract: a second concurrent awaiter `ENGINE_ASSERT`s.
- [x] Implement `CounterAwaitable`: `await_ready()` is true if the count
  is already zero; `await_suspend(h)` (returning `bool`) stores `h` into
  the slot, **then re-checks the count** — if it is zero, it tries to
  take the handle back with an atomic exchange on the slot; if it gets
  the handle it returns `false` (resume immediately, the decrementer
  never saw it), otherwise the decrementer owns the wake-up and it
  returns `true`; `await_resume()` is empty.
- [x] `Decrement()`: when it takes the count to zero it atomically
  exchanges the slot with null and, if it got a handle, reschedules it —
  onto the current participant's deque if called from a participant,
  otherwise via the scheduler's injection queue (Day 31). The exchange on
  both sides guarantees exactly one of {decrementer, awaiter} resumes the
  coroutine — write this invariant in a comment above both functions.
- [x] Write a test for **the race itself**: threads decrement the
  counter to zero at randomized moments around the awaiter's
  `await_ready`/`await_suspend`, 100,000 iterations, asserting the
  continuation runs exactly once every time (no lost wake-up, no double
  resume).
- [x] Wire `Job::SetCompletion(Counter&)` (declared in Day 28) so a
  completing job decrements its counter from `final_suspend`; replace
  Day 28's test-only callback in its test with this.
- [x] Add the `Counter` overload of `Scheduler::HelpUntil` (main waits
  by helping until the counter reaches zero).
- [x] Test: a parent job `Increment`s a shared counter 8 times, submits 8
  child jobs bound to it via `SetCompletion`, then `co_await counter`;
  each child sets a flag, and the parent checks all 8 flags only after it
  resumes past the `co_await`. Run 10,000 times under the default pool
  and under `N = 1` and `N = 2`.
- [x] Run the ordering test and the race test under `jobs_tests_tsan`
  and `jobs_tests_asan`.
- [x] Commit.

**Implementation note (design refinement):** the separate "count + slot
exchange on both sides" scheme in the tasks above has a lifetime hole —
if the awaiter wins the exchange and returns, the Counter (usually a
local in the awaiter's frame) can be destroyed while the decrementer
that reached zero still has to do its own exchange on it. Implemented
instead: ONE atomic word (bit 0 = waiter parked, bits 1.. = count);
`await_suspend` does a single `fetch_or(1)` and `Decrement` a single
`fetch_sub(2)`, and only the side that wins that one RMW touches the
slot, so neither side touches the Counter after losing. Same
exactly-once guarantee, no use-after-free window. The race test frees
the Counter from the awaiter right after the wake-up to prove it.

**Done:** the parent proceeds past `co_await counter` only after all 8
children have completed — 10,000 runs per configuration, no reordering
— and the await/decrement race test shows exactly-once resumption over
100,000 iterations, clean under TSan.

- [x] (from Day 28 review) The completion hook must not resume the waiting parent inline on the finishing job's stack: requeue it or use symmetric transfer, and test a deep continuation chain for stack growth.

## Post-implementation code review (2026-11-03, `/code-review high`)

Fixed: second-awaiter check now happens before `slot_` is written;
race test restructured so the awaiter `delete`s the Counter right after
resumption while the decrementer may still be inside `Decrement()` (ASan
turns any touch-after-wake into a failure) and counts total resumes ==
iterations; both `await_suspend` paths are now covered deterministically
by a hand-driven protocol test (the inline-continue window is only
nanoseconds wide, so a random race test cannot assert it — a first
attempt that did was flaky and was replaced); added path counters
(`Counter::PathStats`); scheduler.h documents that callers must join all
outstanding work before Shutdown (frames parked in a Counter are in no
queue and cannot be freed by it). Day-32 plan file updated with the
single-word-state design refinement (it fixes a lifetime hole in the
original exchange-on-both-sides sketch).
Declined: release-build concerns (null scheduler in `Counter()`, underflow
wrap, double-awaiter) — `ENGINE_ASSERT` is always enabled in this
codebase, so those abort at the point of misuse in every build.
Also resolved: the Day-28 carry-over (no inline resumption) — the stack
test chains 5,000 dependent jobs on one thread and asserts the native
stack spread stays < 64 KiB.
Verified: 100,000-iteration race (N=4 and 2), 10,000 parent/8-child runs
for N=1,2,default, plain/ASan/TSan clean, repeated.
