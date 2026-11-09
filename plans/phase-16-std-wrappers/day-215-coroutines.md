# Phase 16 / Day 215 — Coroutines in `jobs`

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Days 208-209.

`<coroutine>` is used in 9 engine files and 4 tests. Wrap `std::coroutine_handle`, `std::suspend_always`, `std::suspend_never` and the `std::coroutine_traits` specialization (compiler-mandated by name) in one header of the jobs module.

## Tasks

- [ ] `jobs/coroutine.h`: `CoroutineHandle<Promise>` (`Resume`, `Done`, `Destroy`, `Promise`, `FromPromise`, `Address`, `FromAddress`, `operator bool`), `CoroutineHandle<void>`, `SuspendAlways`, `SuspendNever`, `NoopCoroutine()`; the only file that includes `<coroutine>` and defines the `std::coroutine_traits` specialization that maps a return type to `Job::promise_type`.
- [ ] Check what the compilers need: a `promise_type` member found by `std::coroutine_traits` — keep a documented customization point (a `ENGINE_COROUTINE_RETURN_TYPE(Type)` macro that opens `namespace std` once, inside this header only).
- [ ] Port `scheduler.h/.cpp`, `job.h`, `counter.h`, `deque.h`, `async_io.*`, `file_async.*`, and the 4 tests (`counter_test`, `job_test`, `deque_test`, the docgen fixture `coroutine_style.h` stays as a docgen fixture of third-party style and is excluded by name).
- [ ] GCC and Clang both (they differ in coroutine frame elision); MSVC check on Day 219.
- [ ] Benchmarks: the job-system benchmark (spawn/await, steal) within the noise band; one disassembly check that `Resume` is a direct call as before.
- [ ] Tests: a wrapper test resuming, destroying and querying handles against `std::coroutine_handle` as oracle; the existing job tests unchanged except the port; asan and tsan.
- [ ] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** `<coroutine>` is included only by `jobs/coroutine.h`; jobs benchmarks within noise; job tests pass in plain, asan and tsan; docs updated (`docs/guide/jobs.md`).
