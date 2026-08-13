# Phase 1 / Day 15 — Threading Primitives

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 13 (process lifecycle & argv).

**Resolved consistent with `PLAN.md` assumption #2:** thread creation
and synchronization primitives use libc's `pthread` API (`pthread_create`,
`pthread_mutex_t`, `pthread_cond_t`), not a hand-rolled raw `clone()`
call and raw `futex` syscalls. Thread creation/sync isn't something
Foundation deliberately hand-rolls (unlike allocators/containers/
string/math), so per the same reasoning that resolved Phase 0's Day 1/2,
wrapping libc's pthread API directly is simpler and avoids re-solving
problems (stack setup, TLS registration) libc already handles correctly.

## Tasks

- [ ] Implement `Thread` in `/engine/platform/linux/thread.h`/`.cpp`
  wrapping `pthread_create`/`pthread_join`, with an engine-facing API
  (`Thread::Spawn(fn, arg)`, `Thread::Join()`) that hides the pthread
  types — this is the seam later OS ports (Windows/macOS) implement
  against with their own native thread APIs.
- [ ] Implement `Mutex` wrapping `pthread_mutex_t`
  (init/lock/unlock/destroy) and `ConditionVariable` wrapping
  `pthread_cond_t` (wait/signal/broadcast), same engine-facing-wrapper
  pattern.
- [ ] Write a test: a worker thread and the main thread each increment
  a shared counter a large number of times under the mutex.
- [ ] Run the test repeatedly (ideally under ThreadSanitizer, or a high
  repeat count otherwise) and confirm the final counter value is exact
  every run, with no lost updates.
- [ ] Commit.

**Done:** a worker thread and the main thread increment a shared
counter under the mutex with no races across repeated stress runs.
