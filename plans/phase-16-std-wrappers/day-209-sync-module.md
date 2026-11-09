# Phase 16 / Day 209 — `sync`: Atomics, Mutexes, Threads

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Day 208.

A layer-0 module `engine/foundation/sync/` wrapping `<atomic>`, `<mutex>`, `<condition_variable>`, `<thread>`. Zero-cost inline types; the job system and the logger depend on them, so the benchmarks gate this day.

## Tasks

- [x] `sync/atomic.h`: `MemoryOrder` (enum with the six orders), `Atomic<T>` with `Load`, `Store`, `Exchange`, `CompareExchangeWeak/Strong`, `FetchAdd/Sub/And/Or/Xor` for integral and pointer `T`, `Atomic<bool>`; free `AtomicThreadFence`, `AtomicSignalFence`; an `ATOMIC_ALWAYS_LOCK_FREE` check as `static_assert`.
- [x] `sync/mutex.h`: `Mutex`, `RecursiveMutex`, `LockGuard<M>`, `UniqueLock<M>` (with `Lock`, `Unlock`, `TryLock`, `OwnsLock`), `ConditionVariable` (`Wait`, `WaitUntil` with a predicate, `NotifyOne`, `NotifyAll`); no timed `std::chrono` parameter: waits take engine time (Day 214 adds `Duration`; until then milliseconds as `u64`).
- [x] `sync/thread.h`: `Thread` (start from a function and an argument, `Join`, `Detach`, `Joinable`, `HardwareConcurrency`), `ThisThread::Yield/Id/SleepFor`, `ThreadId` value type; names and priorities stay in `os`.
- [x] Thread-safety annotations kept where the standard types had them (`ENGINE_CAPABILITY` style macros if the tree uses them; check `engine/foundation/debug/compiler.h`).
- [x] Tests in `tests/sync/` (phase 16): every operation single-threaded against `std` as oracle, contended counters (8 threads x 1e6 increments) for each `FetchX`, lock exclusion with a non-atomic counter under `tsan`, condition-variable ping-pong, `Thread` join/detach lifetimes; run in plain, asan and tsan.
- [x] Benchmarks: the job-system and log benchmarks from Day 207 rerun with the wrappers used in `engine/jobs` and `engine/foundation/log` as a trial (one file each); no regression beyond the recorded noise band, numbers in this file.
- [x] Docs: headers `///`-documented (`Aborts:` for misuse such as unlocking an unowned mutex), `docs/guide/foundation-sync.md` new page (which type to use, memory-order rules of thumb), `docs/reference/modules.txt`, `ARCHITECTURE.md`, `module_deps_check` map.
- [x] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** the `sync` module and its tests pass in plain, asan and tsan, the trial benchmarks show no regression, docs and guide page are committed, and `ctest -L docs` passes.

## Result

- `engine/foundation/sync/` (`atomic.h`, `mutex.h`, `thread.h`), `tests/sync` plain/asan/tsan, guide page `docs/guide/foundation-sync.md`. `-O2` benchmark (Clang): fetch_add 1.456 vs 1.457 ns, lock+unlock 3.929 vs 3.932 ns, CAS 1.740 vs 1.738 ns (std vs wrapper): zero cost. In Debug the wrapper is 1.4-2.4x (no inlining).
- Review: see Day 208.
