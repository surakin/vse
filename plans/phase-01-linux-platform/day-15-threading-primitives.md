# Phase 1 / Day 15 — Threading Primitives

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 13 (process lifecycle & argv).

**Resolved consistent with `PLAN.md` assumption #2:** thread creation
and synchronization use `std::thread`/`std::mutex`/`std::condition_variable`/
`std::atomic` directly, not a hand-rolled raw `clone()`/`futex` wrapper
and not a custom `Thread`/`Mutex`/`ConditionVariable` engine type either.
Unlike Vulkan/Wayland, these standard-library facilities are already the
portable abstraction across every target platform this engine ships to
— there's no per-platform difference here for a wrapper to paper over,
so wrapping them would just add an indirection layer with no seam left
to justify it. `std::thread`'s constructor can throw `std::system_error`
on failed thread creation; under this project's `-fno-exceptions` that
surfaces as `std::terminate` rather than a catchable error, which is
accepted as fine — failed thread creation is an unrecoverable
resource-exhaustion condition regardless.

## Tasks

- [x] Write a test: spawn a `std::thread` that, together with the main
  thread, increments a shared counter a large number of times under a
  `std::mutex`.
- [x] Write a second test using `std::atomic<int>` for the same
  shared-counter increment, without a mutex, to confirm atomics work
  correctly under this toolchain/build flags too.
- [x] Write a third test using `std::condition_variable` to hand off
  work between the main thread and a worker thread (e.g. producer signals
  "data ready", consumer waits on the condition variable before reading).
  All three in `tests/foundation/concurrency_primitives_test.cpp`
  (500,000 increments/thread for the mutex/atomic tests — high enough
  that a lost update from a missing lock/fence would show up reliably).
  `find_package(Threads REQUIRED)` + `Threads::Threads` added to
  `tests/foundation/CMakeLists.txt` for `std::thread` linkage. No
  production code — this day only exercises the standard library
  directly under this project's build flags/toolchain, per the
  already-resolved decision above not to add an engine wrapper type, so
  there's no RED/GREEN cycle (nothing new of ours to fail first).
- [x] Run all three tests repeatedly (ideally under ThreadSanitizer, or
  a high repeat count otherwise) and confirm no races/lost updates.
  ThreadSanitizer skipped (would need a `foundation_tests_tsan`-style
  target, not yet worth the setup); ran the `[concurrency]` tests 30x
  with randomized ordering (`--order rand`) instead — 0 failures across
  90 test-case executions (3 tests x 30 runs).
- [x] Commit.

**Done:** all three tests (mutex, atomic, condition-variable handoff)
pass repeatedly with no races across stress runs.
