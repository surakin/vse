# Foundation: Synchronization

Status: draft (Phase 16, Day 209: the module; the port of the tree is Day 210).

`engine/foundation/sync/` wraps the standard library's `<atomic>`, `<mutex>`, `<condition_variable>` and `<thread>`. The wrappers are inline forwarders: a loop of `Atomic::FetchAdd`, `LockGuard` or `Atomic::CompareExchangeWeak` compiles to the same code as the standard type (`-O2`, Clang: 1.46 ns per relaxed `fetch_add` both ways, 3.93 ns per lock/unlock, 1.74 ns per CAS; the side-by-side benchmark is `tests/sync/sync_test.cpp`, tag `[benchmark]`). Code outside this module does not include the standard concurrency headers or spell `std::atomic`, `std::mutex` and the like; see `PLAN.md` assumption #23.

## Which type do I use?

| I need | Use |
|---|---|
| a counter, flag, index or pointer several threads read and write | `Atomic<T>` with a [[MemoryOrder]] |
| to protect several variables with one lock | [[Mutex]] and `LockGuard lock(mutex);` |
| to take the same lock again from the same thread | [[RecursiveMutex]] (prefer restructuring) |
| to unlock early, relock, or wait on a condition | `UniqueLock<Mutex>` and [[ConditionVariable]] |
| a thread | [[Thread]] (`Join` or `Detach` before it is destroyed) |
| the calling thread's id, a yield or a short sleep | `ThisThread::Id()`, `Yield()`, `SleepForMilliseconds(n)` |
| initialization that runs once across threads | [[OnceFlag]] and [[CallOnce]] |

## Memory orders

`MemoryOrder::Relaxed` for counters nobody orders against; `Release` on the store and `Acquire` on the load that publish and consume data; `AcqRel` for a read-modify-write that does both; `SeqCst` (the default) when unsure. `CompareExchangeWeak(expected, desired, order)` derives the failure order itself.

## Source of truth

- `engine/foundation/sync/atomic.h`
- `engine/foundation/sync/mutex.h`
- `engine/foundation/sync/thread.h`

Full reference: [Foundation: Synchronization](../reference/foundation-sync.md).
