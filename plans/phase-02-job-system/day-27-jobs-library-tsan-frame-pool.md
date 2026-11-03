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

- [x] Create `/engine/jobs/` as a static library `jobs` linking
  `foundation` (header search path `engine/`-relative, same pattern as
  `foundation`), and `/tests/jobs/` with a `jobs_tests` Catch2
  executable registered with `ctest`. Add a trivial smoke test so the
  target builds and runs. Headers are included as `"jobs/<name>.h"`
  (include root `engine/`). Toolchain: GCC 16.2.1.
- [x] Add sanitizer variants mirroring `foundation_asan`: `jobs_asan` +
  `jobs_tests_asan` (address/undefined) and `jobs_tsan` +
  `jobs_tests_tsan` (`-fsanitize=thread`; a **separate** build because
  TSan cannot be combined with ASan). Foundation must also be built with
  TSan for the TSan variants (`foundation_tsan`). Register the TSan
  tests with a `tsan` ctest label. Done via one helper function in
  `tests/jobs/CMakeLists.txt`; ctest names carry an ` [asan]`/` [tsan]`
  suffix, and `ctest -L tsan` runs the TSan set.
- [x] Prove the TSan target actually detects races: add a deliberately
  racy test (two threads incrementing a plain `int`), confirm TSan
  reports it and the run fails, then mark it so it never runs in normal
  `ctest` as a ctest **expected failure**. *Deviation from the original wording:*
  instead of a hidden Catch2 `[.]` test (which `catch_discover_tests`
  would still register and run), it is a standalone `tsan_canary`
  executable registered with `WILL_FAIL TRUE` — so ctest goes **red** if
  TSan ever stops detecting the race. Verified: TSan reports it and the
  process exits 66.
- [x] Implement `FramePool` in `/engine/jobs/frame_pool.h`/`.cpp`:
  fixed size classes (128, 256, 512, 1024, 2048 bytes — revisit in Day 35
  with real frame sizes) each backed by foundation `PoolAllocator`
  slabs behind a central `std::mutex`, plus a **per-thread cache** (an
  intrusive free list per class, no synchronization; **only the
  process-wide `FramePool::Global()` uses them** — a thread cache is keyed
  to one pool, asserted — local pools used by tests are central-only) that refills from /
  spills to the central pool in batches. A block freed on a different
  thread than it was allocated on simply joins the freeing thread's
  cache (blocks of one class are interchangeable).
- [x] Define the exhaustion/oversize policy: a request larger than the
  top class, or arriving when the central pool is exhausted, falls back
  to libc `malloc` (never returns `nullptr`; `ENGINE_ASSERT` if even
  `malloc` fails), and increments a fallback counter. Frees must be able
  to tell pooled blocks from fallback blocks and route them correctly —
  chosen: a 16-byte header (class index + magic) on every block, which
  also keeps results 16-byte aligned, catches foreign pointers and
  double frees (magic cleared on free).
- [x] Expose instrumentation: per-class alloc/free counts and the
  fallback count via `FramePool::Stats()` (atomic counters), for Day 28's
  "frame came from the pool" check and Day 35's size-class tuning.
- [x] Tests: single-thread alloc/free of every class; 8 threads doing
  alloc-on-A / free-on-B churn for 1M operations with no overlapping live
  blocks (write a unique tag into each live block and verify it before
  freeing); oversize and exhaustion fallbacks hit and are freed
  correctly. Run under `jobs_tests_tsan` and `jobs_tests_asan` with zero
  reports. 7 test cases / 198 assertions; the churn test uses a 4096-slot
  mailbox of atomics so blocks are freed by a different thread than
  allocated them, with head/tail/pattern tags to detect overlap. Also run
  20× each under TSan and ASan with no failures.
- [x] Commit.

**Done:** `jobs`, `jobs_tests` and the ASan/TSan variants build; the TSan
canary is confirmed to fail under TSan; and `FramePool` survives the
8-thread cross-thread alloc/free churn clean under both sanitizers, with
its stats showing pooled allocations and exercised fallbacks.
