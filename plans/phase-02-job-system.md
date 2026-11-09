# Phase 2 — Job System & Coroutines

**Depends on:** Phase 1 (Linux platform layer) — uses `std::thread`,
`std::mutex`, and `std::condition_variable` (Day 15, `PLAN.md`
assumption #2) as the underlying primitives the worker pool is built on.
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

**Status: COMPLETE (2026-11-03)** — all of Days 27-35 implemented and reviewed.

**Plan status:** revised 2026-11-03 after a verification pass (Day 27
added; former Days 27-34 are now 28-35; design decisions below settled).

See root `PLAN.md` assumption #15 for why coroutines were chosen over
stackful fibers.

## Days

Each day is its own file with a specific, checkable sub-task list and a
Done condition — see `CLAUDE.md`'s "How the plan is structured."

27. [Jobs library skeleton, ThreadSanitizer target & frame pool](phase-02-job-system/day-27-jobs-library-tsan-frame-pool.md)
28. [Job coroutine type, ownership & completion](phase-02-job-system/day-28-job-coroutine-type.md)
29. [Participants, topology & worker thread pool](phase-02-job-system/day-29-worker-thread-pool.md)
30. [Work-stealing deque](phase-02-job-system/day-30-work-stealing-deque.md)
31. [Scheduler loop, idle parking & main-thread help](phase-02-job-system/day-31-scheduler-loop.md)
32. [Counter/await mechanism](phase-02-job-system/day-32-counter-await.md)
33. [Fan-out/join helper](phase-02-job-system/day-33-fan-out-join-helper.md)
34. [Async I/O integration](phase-02-job-system/day-34-async-io-integration.md)
35. [Stress & throughput test suite](phase-02-job-system/day-35-stress-throughput-tests.md)

## Design decisions (settled 2026-11-03)

Made during plan verification; the day files implement them. Change them
here first if implementation proves one wrong.

- **D1 — Worker model: "main helps while waiting."** `N` *participants*
  = `N-1` spawned worker threads + the main thread. `N` defaults to the
  number of **logical CPUs** in the process's affinity mask
  (`sched_getaffinity`; no physical-core/SMT topology), overridable by
  the `ENGINE_JOB_THREADS` env var or an explicit constructor argument
  (tests). Workers are pinned to distinct allowed CPUs (excluding the
  first, left to main); main is **not pinned** and is **not** a
  permanent worker — it runs jobs only inside `Scheduler::HelpUntil`,
  so it stays free for the window/frame loop. `N == 1` (no spawned
  threads) is valid. `HelpUntil` is main-only and never called from
  inside a job (jobs wait with `co_await`).
- **D2 — Job lifetime & completion.** Jobs start suspended
  (`initial_suspend = suspend_always`). Submitting a `Job` transfers the
  frame to the scheduler; a completing job decrements its bound
  `Counter` from `final_suspend` and then destroys its own frame; an
  unsubmitted `Job` destroys its frame. Jobs return `void` (results go
  through pointers to data that outlives the join). Directly
  `co_await`-ing a `Job` is out of scope — joining is done with
  `Counter`. Coroutine bodies take state by value/pointer; never
  capture-by-reference lambdas.
- **D3 — Frame allocation.** Foundation's `PoolAllocator` is
  single-threaded, fixed-size, returns `nullptr` when full, and commits
  its whole slab on construction, so jobs use a dedicated `FramePool`: a
  few size classes, each an up-front *reserved* (uncommitted) slab handed
  out by bump pointer + an intrusive free list behind a central mutex,
  per-thread caches (late frees after a thread's cache is destroyed fall
  back to the central pool), and a counted libc-`malloc` fallback for
  oversize/exhausted cases (never `nullptr`).
- **D4 — Idle strategy.** Workers try own deque → injection queue →
  bounded random steals, back off with `yield()` for a bounded number of
  rounds, then park on a condition variable; `Submit` wakes a parked
  worker, with a documented lost-wakeup-safe protocol.
- **D5 — External submission.** Threads that are not participants (the
  I/O thread, any other) submit through a fixed-capacity mutex-guarded
  injection queue; a full participant deque spills there too. Overflow
  of both is an assertion failure, not silent loss.
- **D6 — Victim selection.** Per-participant deterministic xorshift32
  seeded from the participant index.
- **D7 — `Counter`.** Single awaiter by contract; atomic count plus
  atomic continuation slot; both `await_suspend` and the final
  `Decrement` race on an exchange of that slot so exactly one resumes
  the coroutine. Rescheduling from a non-participant goes through the
  injection queue (no "fixed dispatch worker").
- **D8 — Async I/O.** Blocking reads run on a dedicated I/O thread that
  is *not* a participant; completion is delivered via `Counter::Decrement`
  so the awaiting coroutine always resumes on a participant.
- **D9 — Sanitizers.** ASan/UBSan and a separate ThreadSanitizer build
  (TSan can't combine with ASan) for the `jobs` library, foundation and
  tests; the TSan target is validated by a deliberate-race canary
  (Day 27). Atomics are written with ordering on the operations, not
  standalone fences, because TSan does not model fences.

## Notes / open questions

- The `FramePool` size classes (Day 27) are a starting guess; Day 35
  logs the real frame-size histogram and adjusts them. Revisit again
  once ECS (Phase 18) and asset streaming (Phase 21) are real consumers.
- `parallel_for`'s chunk-size heuristic (Day 33) is a fixed constant for
  now; adaptive chunking based on measured per-job cost is deferred until
  a real workload (Phase 18's ECS system scheduling) shows it's needed.
- Stackful fibers were considered and explicitly rejected for this phase
  — see root `PLAN.md` assumption #15 for the portability reasoning.
- Physical-core/SMT-aware placement is deliberately not done (logical
  CPUs only). Revisit if profiling on a hyperthreaded machine shows
  siblings contending.
- Windows/macOS/console ports (Phases 13, 23, 24 and 25) will need their own
  affinity/topology queries behind the same participant interface; the
  Linux-specific calls are confined to Day 29.
