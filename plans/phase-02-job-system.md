# Phase 2 — Job System & Coroutines

**Depends on:** Phase 1 (Linux platform layer) — uses its raw thread
creation (`clone()`) and futex-based mutex/condvar as the underlying OS
primitives the worker pool is built on.
**Goal:** a portable, work-stealing job scheduler with C++20 coroutines
as the sole suspension mechanism — no hand-written per-ABI
context-switching code anywhere. This is the layer every later
multithreaded subsystem (ECS parallel system scheduling, renderer
command recording, physics, audio, asset streaming) schedules work
through, rather than each hand-rolling its own thread pool.
**Definition of done:** thousands of jobs with randomized dependency
chains run correctly and race-free under repeated stress runs (with
thread/address sanitizers), work-stealing measurably improves throughput
on skewed load, and a chained `co_await` async-I/O-style job resolves in
the correct order.

See root `PLAN.md` assumption #15 for why coroutines were chosen over
stackful fibers.

## Tasks

- [ ] **Day 27 — Job/Task coroutine type & promise_type.** Define a `Job`
  return type usable as a C++20 coroutine return type: `promise_type`
  with `initial_suspend`/`final_suspend`, `get_return_object`,
  `unhandled_exception` (routes to the Day 9 assert/log path — no C++
  exceptions per PLAN.md scope). Override `promise_type::operator
  new/delete` to allocate coroutine frames from a dedicated Foundation
  pool allocator (Phase 0 Day 5) sized for typical job-frame sizes,
  instead of the global `new`.
  **Done:** a coroutine written as `Job Example() { co_return; }`
  compiles and runs, and instrumenting the allocator confirms its frame
  came from the pool, not the global default.

- [ ] **Day 28 — Worker thread pool.** Spawn one OS thread per physical
  core (Phase 1 Day 20's `clone()`-based thread creation), querying core
  count via `sched_getaffinity`/`/proc/cpuinfo` parsing (no libc
  `std::thread::hardware_concurrency`); pin each worker to a core with
  `sched_setaffinity` where available. The calling (main) thread also
  registers itself as a worker rather than idling.
  **Done:** launching the pool on an N-core machine logs N+1 active
  workers (N spawned + main), each pinned to a distinct core, verified
  via `/proc/self/status`'s `Cpus_allowed_list` per thread.

- [ ] **Day 29 — Work-stealing deque.** Lock-free Chase-Lev double-ended
  deque holding `std::coroutine_handle<>`, backed by a fixed-capacity
  array (no dynamic resize — matches the no-malloc-by-default rule).
  Owner pushes/pops its own bottom (LIFO); thieves pop from the top
  (FIFO) via CAS.
  **Done:** a single-producer/single-consumer stress test (owner
  pushing/popping while a thief steals concurrently) run under
  ThreadSanitizer shows zero races and the deque never yields a
  duplicate or dropped handle across 1M operations.

- [ ] **Day 30 — Scheduler loop.** Each worker's main loop: pop from own
  deque; if empty, attempt to steal from a random peer (bounded retries
  before a brief backoff); resume the popped/stolen `coroutine_handle`
  via `.resume()`. A global shutdown flag is checked each iteration for
  clean pool teardown.
  **Done:** submitting 10,000 trivial jobs (each incrementing an atomic
  counter and returning) to the pool completes and the counter matches
  exactly 10,000, run 100 times with no flakiness.

- [ ] **Day 31 — Counter/await mechanism.** `Counter` type wrapping an
  atomic int; a `CounterAwaitable` with `await_ready()` (true if count ==
  0), `await_suspend(coroutine_handle<> h)` (registers `h` as the
  counter's continuation, returns to the scheduler without resuming `h`),
  and `await_resume()`. Decrementing the counter to zero reschedules the
  registered continuation onto the current worker's deque (or, if none,
  a fixed dispatch worker).
  **Done:** a parent job that spawns 8 child jobs each decrementing a
  shared `Counter`, then does `co_await counter`, only proceeds past the
  `co_await` after all 8 children have completed — verified by an
  ordering assertion (a flag set by children, read only after the parent
  resumes) across 10,000 repeated runs with no reordering.

- [ ] **Day 32 — Fan-out/join helper.** A `parallel_for(count,
  chunk_size, fn)` helper built purely from Day 28-31 primitives: splits
  `count` work items into jobs of `chunk_size`, submits them all against
  one shared `Counter`, and returns a `Job` that `co_await`s that
  counter.
  **Done:** `parallel_for` over a 100,000-element array (each job summing
  its chunk into a per-chunk result, combined after the join) produces
  the same total as a serial sum, run under multiple worker-count
  configurations (1, 2, N cores).

- [ ] **Day 33 — Async I/O integration.** A `co_await`-able async
  file-read wrapper on top of Phase 1 Day 21's file I/O (the synchronous
  read is dispatched onto a dedicated I/O job so it doesn't block a
  compute worker; the result is delivered via the Day 31
  Counter/continuation mechanism, not a raw OS callback thread).
  **Done:** `co_await ReadFileAsync(path)` inside a job returns the
  correct file contents, and the awaiting coroutine resumes on a
  job-system worker (verified by logging the resuming thread's ID is one
  of the registered workers, never an ad hoc OS thread).

- [ ] **Day 34 — Stress & throughput test suite.** Formal test suite
  (using Phase 0 Day 12's test harness): (a) randomized
  dependency-graph stress test — thousands of jobs with random fan-out/
  join shapes, repeated under ThreadSanitizer, asserting no
  races/deadlocks and exactly-once execution per job; (b) work-stealing
  throughput test — submit heavily skewed load (all jobs queued on
  worker 0), verify other workers steal and measure wall-clock scaling
  vs. a single-worker baseline; (c) nested `co_await` ordering test — a
  chain of 5 dependent async steps resolves in strict order across
  10,000 runs.
  **Done:** `ctest` runs all three tests green, and the throughput test's
  log shows measured speedup (not just "didn't crash") scaling with core
  count.

## Notes / open questions

- Coroutine frame sizes vary by job complexity (capture list size, local
  variables across suspension points); the Day 27 pool allocator's size
  class(es) may need tuning once Day 34's real workloads exist — not a
  blocker for this phase, flagged for revisit once ECS (Phase 5) and
  asset streaming (Phase 8) are real consumers.
- `parallel_for`'s chunk-size heuristic (Day 32) is a fixed constant for
  now; adaptive chunking based on measured per-job cost is deferred until
  a real workload (Phase 5's ECS system scheduling) shows it's needed.
- Stackful fibers were considered and explicitly rejected for this phase
  — see root `PLAN.md` assumption #15 for the portability reasoning.
