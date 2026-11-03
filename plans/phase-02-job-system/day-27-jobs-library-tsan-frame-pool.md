# Phase 2 / Day 27 — Jobs Library Skeleton, ThreadSanitizer Target & Frame Pool

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Phase 1 complete (Phase 0 Day 5's `PoolAllocator`, Phase 0
Day 9's assert/log path, Phase 0 Day 15's Catch2 harness).

Added when the Phase 2 plan was verified (2026-11-03): three things every
later day silently needed but no day owned — a place for the code to live,
a ThreadSanitizer build (Days 30, 32, 35 all call for TSan runs), and a
coroutine-frame allocator that is actually safe to use from many threads
(`PoolAllocator` is single-threaded with one fixed slot size and returns
`nullptr` when full; coroutine frames are allocated on one worker and
freed on another). See the phase file's "Design decisions" **D3**.

## Tasks

- [ ] Create `/engine/jobs/` as a static library `jobs` linking
  `foundation` (header search path `engine/`-relative, same pattern as
  `foundation`), and `/tests/jobs/` with a `jobs_tests` Catch2
  executable registered with `ctest`. Add a trivial smoke test so the
  target builds and runs.
- [ ] Add sanitizer variants mirroring `foundation_asan`: `jobs_asan` +
  `jobs_tests_asan` (address/undefined) and `jobs_tsan` +
  `jobs_tests_tsan` (`-fsanitize=thread`; a **separate** build because
  TSan cannot be combined with ASan). Foundation must also be built with
  TSan for the TSan variants (`foundation_tsan`). Register the TSan
  tests with a `tsan` ctest label.
- [ ] Prove the TSan target actually detects races: add a deliberately
  racy test (two threads incrementing a plain `int`), confirm TSan
  reports it and the run fails, then mark it so it never runs in normal
  `ctest` (hidden `[.]` tag, documented as the TSan canary). If TSan
  does not fire, the target is not usable and the day is not done.
- [ ] Implement `FramePool` in `/engine/jobs/frame_pool.h`/`.cpp`:
  fixed size classes (128, 256, 512, 1024, 2048 bytes — revisit in Day 35
  with real frame sizes) each backed by foundation `PoolAllocator`
  slabs behind a central `std::mutex`, plus a **per-thread cache** (an
  intrusive free list per class, no synchronization) that refills from /
  spills to the central pool in batches. A block freed on a different
  thread than it was allocated on simply joins the freeing thread's
  cache (blocks of one class are interchangeable).
- [ ] Define the exhaustion/oversize policy: a request larger than the
  top class, or arriving when the central pool is exhausted, falls back
  to libc `malloc` (never returns `nullptr`; `ENGINE_ASSERT` if even
  `malloc` fails), and increments a fallback counter. Frees must be able
  to tell pooled blocks from fallback blocks (address-range check against
  the slabs, or a header) and route them correctly.
- [ ] Expose instrumentation: per-class alloc/free counts and the
  fallback count via `FramePool::Stats()` (atomic counters), for Day 28's
  "frame came from the pool" check and Day 35's size-class tuning.
- [ ] Tests: single-thread alloc/free of every class; 8 threads doing
  alloc-on-A / free-on-B churn for 1M operations with no overlapping live
  blocks (write a unique tag into each live block and verify it before
  freeing); oversize and exhaustion fallbacks hit and are freed
  correctly. Run under `jobs_tests_tsan` and `jobs_tests_asan` with zero
  reports.
- [ ] Commit.

**Done:** `jobs`, `jobs_tests` and the ASan/TSan variants build; the TSan
canary is confirmed to fail under TSan; and `FramePool` survives the
8-thread cross-thread alloc/free churn clean under both sanitizers, with
its stats showing pooled allocations and exercised fallbacks.
