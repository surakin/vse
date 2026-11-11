# Foundation: Synchronization

Status: written (Phase 16).

**Audience:** every engine programmer who shares data between threads. Read [Jobs](jobs.md) first: most engine code runs as jobs, and the job system is the
right tool for spreading work out. This page covers the lower-level pieces underneath it: atomics, locks and threads.

Several threads touching the same variable at the same time, with no rule about who goes first, is a **data race**, and the program's behavior is then undefined
(it may work for a year and then corrupt memory). The types on this page are the rules: an atomic makes one variable safe to share, a mutex makes a group of
variables safe to share, and a condition variable lets a thread sleep until another thread has changed something.

`engine/foundation/sync/` wraps the standard library's `<atomic>`, `<mutex>`, `<condition_variable>` and `<thread>`. The wrappers are inline forwarders: a loop of `Atomic::FetchAdd`, `LockGuard` or `Atomic::CompareExchangeWeak` compiles to the same code as the standard type (`-O2`, Clang: 1.46 ns per relaxed `fetch_add` both ways, 3.93 ns per lock/unlock, 1.74 ns per CAS; the side-by-side benchmark is `tests/sync/sync_test.cpp`, tag `[benchmark]`). Code outside this module does not include the standard concurrency headers or spell `std::atomic`, `std::mutex` and the like; see `PLAN.md` assumption #23.

## At a glance

| Type | What it is | Header |
|---|---|---|
| [[Atomic]]`<T>` | one variable that threads may read and write without a lock | `sync/atomic.h` |
| [[Mutex]], [[RecursiveMutex]] | a lock only one thread holds at a time | `sync/mutex.h` |
| [[LockGuard]], [[UniqueLock]] | hold a lock for a scope; `UniqueLock` can also unlock early and wait | `sync/mutex.h` |
| [[ConditionVariable]] | sleep until another thread signals a change | `sync/mutex.h` |
| [[OnceFlag]], [[CallOnce]] | run an initialization exactly once | `sync/mutex.h` |
| [[Thread]], `ThisThread` | start a thread; ask about the calling thread | `sync/thread.h` |

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

## Using them

An atomic counter shared by many threads:

```cpp
Atomic<u32> finished{0};
// on each worker thread:
finished.FetchAdd(1, MemoryOrder::Relaxed);
// on the main thread:
u32 done = finished.Load(); // SeqCst by default
```

A lock around two variables that must change together. The guard unlocks when the scope ends, even on an early `return`:

```cpp
Mutex mutex;
Vector<u32> queue;
u32 queued_total = 0;

void Add(u32 value) {
    LockGuard<Mutex> lock(mutex);
    queue.Push(value);
    ++queued_total;
}
```

Waiting for work with a condition variable. Always give the wait a predicate (the condition you are waiting for), because a wait can wake without anyone
signalling it:

```cpp
// consumer
UniqueLock<Mutex> lock(mutex);
cv.Wait(lock, [&] { return !queue.Empty(); });
// ... take an item, the lock is held here ...

// producer
{
    LockGuard<Mutex> guard(mutex);
    queue.Push(value);
}
cv.NotifyOne();
```

A thread must be joined (or detached) before its object is destroyed:

```cpp
Thread worker([] { /* work */ });
worker.Join();
```

## Rules

- Prefer the job system ([Jobs](jobs.md)) to your own threads. Create a [[Thread]] only for something that must own an operating-system thread.
- A [[Mutex]] is not recursive: locking it twice from one thread deadlocks. Restructure the code rather than reaching for [[RecursiveMutex]].
- Unlock must come from the thread that locked. Use [[LockGuard]] so you never call `Unlock` by hand.
- A condition variable's wait needs a [[UniqueLock]] that you hold. [[ConditionVariable]] works with [[Mutex]] only.
- `Thread` passes copies of its arguments to the function. Pass a pointer if the thread must change something of yours, and make sure it outlives the thread.
- `AtomicThreadFence` does not build under GCC with ThreadSanitizer; use an atomic operation with the order you need instead.
- On macOS a [[Thread]] gets the default stack of a secondary thread, 512 KiB (Linux: 8 MiB): keep large buffers off the stack. A thread's name can
  only be set from inside it there (`pthread_setname_np` takes no thread); the job system's workers do so (Phase 26 Day 305, [Jobs](jobs.md)).
- Under the sanitizers on macOS a report aborts the process (`abort_on_error` defaults to 1 there, 0 on Linux).

## Common mistakes

- **Reading a shared variable without a lock or an atomic** "because only one thread writes it". It is still a data race.
- **Forgetting to join.** Destroying a joinable [[Thread]] terminates the program.
- **Waiting without a predicate**, or with an `if` instead of a loop. A spurious wake then continues with the condition false.
- **Notifying before changing the data**, or changing the data without the mutex. The waiting thread can miss the change and sleep forever.
- **Using `Relaxed` to publish data.** Relaxed only makes the counter itself correct; the other writes need a `Release` store and an `Acquire` load.
- **Holding a lock while calling unknown code** (a callback, a job, a file read). That is how deadlocks start. Copy what you need out, unlock, then call.
- **Two locks taken in different orders** in different places.

## See also

[Jobs](jobs.md), [Containers](foundation-containers.md), [Standard types](foundation-types.md).

## Source of truth

- `engine/foundation/sync/atomic.h`
- `engine/foundation/sync/mutex.h`
- `engine/foundation/sync/thread.h`

Full reference: [Foundation: Synchronization](../reference/foundation-sync.md).
