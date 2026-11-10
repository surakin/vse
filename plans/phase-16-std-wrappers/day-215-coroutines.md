# Phase 16 / Day 215 — Coroutines in `jobs`

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Days 208-209.

`<coroutine>` is used in 9 engine files and 4 tests. Wrap `std::coroutine_handle`, `std::suspend_always`, `std::suspend_never` and the `std::coroutine_traits` specialization (compiler-mandated by name) in one header of the jobs module.

## Tasks

- [x] `jobs/coroutine.h`: `CoroutineHandle<Promise>` (`Resume`, `Done`, `Destroy`, `Promise`, `FromPromise`, `Address`, `FromAddress`, `operator bool`), `CoroutineHandle<void>`, `SuspendAlways`, `SuspendNever`, `NoopCoroutine()`; the only file that includes `<coroutine>` and defines the `std::coroutine_traits` specialization that maps a return type to `Job::promise_type`.
- [x] Check what the compilers need: a `promise_type` member found by `std::coroutine_traits` — keep a documented customization point (a `ENGINE_COROUTINE_RETURN_TYPE(Type)` macro that opens `namespace std` once, inside this header only).
- [x] Port `scheduler.h/.cpp`, `job.h`, `counter.h`, `deque.h`, `async_io.*`, `file_async.*`, and the 4 tests (`counter_test`, `job_test`, `deque_test`, the docgen fixture `coroutine_style.h` stays as a docgen fixture of third-party style and is excluded by name).
- [ ] GCC and Clang both (they differ in coroutine frame elision); MSVC check on Day 219.
- [ ] Benchmarks: the job-system benchmark (spawn/await, steal) within the noise band; one disassembly check that `Resume` is a direct call as before.
- [x] Tests: a wrapper test resuming, destroying and querying handles against `std::coroutine_handle` as oracle; the existing job tests unchanged except the port; asan and tsan.
- [x] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** `<coroutine>` is included only by `jobs/coroutine.h`; jobs benchmarks within noise; job tests pass in plain, asan and tsan; docs updated (`docs/guide/jobs.md`).

## Result

- `engine/jobs/coroutine.h` holds `CoroutineHandle<Promise>`, `SuspendAlways` and `SuspendNever` as aliases of the standard types and is the only file that includes `<coroutine>`; the aliases keep the standard member functions (`resume`, `done`, `destroy`, `address`, `from_address`, `from_promise`), which the compiler looks up by name. No `std::coroutine_traits` specialization is needed: `Job` has a nested `promise_type`, so the plan's `ENGINE_COROUTINE_RETURN_TYPE` macro was not written. 12 files ported; new test `CoroutineHandle resumes, reports done, round-trips its address and destroys` (plain, asan, tsan). GCC found two defects in earlier tests, fixed: `atomic_thread_fence` is rejected under `-fsanitize=thread` (`-Werror=tsan`; the fence test is skipped in the tsan flavor), and a `snprintf` oracle warned `-Wformat-truncation` (routed through `vsnprintf`). **Not done:** the jobs benchmark comparison and the MSVC check (Day 219).
- Scoped runs: plain `ran 1969 of 5054 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 1453 of 5054 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`, tsan `ran 1135 of 5054 tests (phases: all; flavors: tsan; display: included; crash tests: excluded)`; only the two known X11 window tests fail.

## Post-implementation code review (`/code-review high`)

0 findings: the engine diff is a rename of `std::coroutine_handle`/`std::suspend_*` to the aliases plus the include swap.
