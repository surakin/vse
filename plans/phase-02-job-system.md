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

See root `PLAN.md` assumption #15 for why coroutines were chosen over
stackful fibers.

## Days

Each day is its own file with a specific, checkable sub-task list and a
Done condition — see `CLAUDE.md`'s "How the plan is structured."

27. [Job/Task coroutine type & promise_type](phase-02-job-system/day-27-job-coroutine-type.md)
28. [Worker thread pool](phase-02-job-system/day-28-worker-thread-pool.md)
29. [Work-stealing deque](phase-02-job-system/day-29-work-stealing-deque.md)
30. [Scheduler loop](phase-02-job-system/day-30-scheduler-loop.md)
31. [Counter/await mechanism](phase-02-job-system/day-31-counter-await.md)
32. [Fan-out/join helper](phase-02-job-system/day-32-fan-out-join-helper.md)
33. [Async I/O integration](phase-02-job-system/day-33-async-io-integration.md)
34. [Stress & throughput test suite](phase-02-job-system/day-34-stress-throughput-tests.md)

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
