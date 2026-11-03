# Phase 2 / Day 33 — Fan-Out/Join Helper

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Day 32 (Counter/await mechanism) and Day 29 (pool-size
override via `ENGINE_JOB_THREADS` / explicit `N`).

## Tasks

- [ ] Decide and document the callable representation. The foundation
  has no `std::function` and a lambda can't be a safe coroutine body (see
  Day 28's parameter-lifetime rule), so `parallel_for` takes a plain
  function pointer plus a `void* user` context (`void (*fn)(void* user,
  usize begin, usize end)`), with a thin typed wrapper for convenience
  that stores the callable on the caller's frame. State the lifetime
  requirement: `user` and everything it points to must outlive the join.
- [ ] Implement `parallel_for(count, chunk_size, fn, user)` in
  `/engine/jobs/parallel_for.h`, built purely from Days 28-32's
  primitives: split `count` into jobs of `chunk_size` (last chunk may be
  short; `count == 0` and `chunk_size >= count` handled), `Increment` one
  shared `Counter` per chunk, submit each chunk job bound to it, and
  return a `Job` that `co_await`s that counter. A main-thread overload
  that submits and then `HelpUntil`s the counter is provided for callers
  that are not themselves jobs.
- [ ] Chunk-size heuristic stays a fixed constant (see phase Notes); the
  caller can always pass an explicit `chunk_size`.
- [ ] Test: sum a 100,000-element array (values chosen so the exact sum
  is known in closed form) via `parallel_for` — each chunk writes its
  partial sum to its own slot of a results array, combined after the
  join; compare to the serial sum for **exact** equality.
- [ ] Edge-case tests: `count == 0`, `count == 1`, `chunk_size == 1`,
  `chunk_size > count`, `count` not a multiple of `chunk_size`.
- [ ] Run under explicit pool sizes `N = 1`, `2`, and the default (all
  logical CPUs), plus once via `ENGINE_JOB_THREADS=3` to cover the env
  override path; results identical in every configuration.
- [ ] Run under `jobs_tests_tsan` and `jobs_tests_asan`.
- [ ] Commit.

**Done:** `parallel_for` over a 100,000-element array equals the serial
sum exactly under `N = 1`, `2`, `3` (env override) and the default pool,
edge cases pass, and TSan/ASan are clean.
