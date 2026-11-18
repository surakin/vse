# Phase 27 / Day 460 — Latent Waits: Coroutines in Scripts

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 446, 457-459.

Scripts can wait without blocking: the landing target for Blueprint's `Delay`, `Timeline` and async nodes.

## Tasks

- [x] Each script callback runs in a coroutine when it asks to wait: `wait(seconds)`, `wait_frames(n)`, `wait_event(name, [timeout])`, `wait_all(...)`, `wait_any(...)`, `wait_task(handle)` (Day 461); `spawn(fn)` starts a script-level coroutine owned by the instance, `stop(handle)` cancels it.
- [x] The scheduler (in the partition, in C): a wait list keyed by wake time (the simulation clock) and by event; woken coroutines resume at the start of the next phase in a stable order (wake time, instance id, creation sequence); no OS timers and no thread ever blocks.
- [x] Cancel rules: destroying an entity cancels its coroutines without running them past the next yield (the `on_destroy` callback may not yield); a disabled instance's waits are suspended and resume with their remaining time; hot reload (Day 464) and a state reset cancel them with an `on_cancel` hook.
- [x] Timeline-like helpers built on it for Phase 32: `tween(from, to, seconds, easing, fn)`, `every(seconds, fn)`, `after(seconds, fn)`; they are plain library code over `wait`.
- [x] Cost: 100,000 waiting coroutines (memory per coroutine, wake cost per tick) recorded; a per-partition cap on live coroutines (setting) with a clear error.
- [x] Tests: ordering, cancellation, waits across hot reload (cancelled, not crashed), equal results at 1 and 16 threads, a coroutine that never waits hits the budget of Day 449.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Scripts wait for time, frames, events and tasks with coroutines, cancelled correctly and ordered the same on any thread count.

## Notes

Event waiters live in their own list so event delivery does not scan timers. `__spawn` is accepted only from the owning entity, once, for a task it owns. Deferred items are in KNOWN-ISSUES.md.

## Post-implementation code review

Findings: event delivery scanned all timer waiters (fixed: separate list); the internal `__spawn` event could be forged by another script (fixed with a sender, owner and one-shot check, with a regression test that fails without the fix); the automated security review's self-addressed forgery variant (fixed the same way, test extended).

Runs: `scripts/test-phase.sh 27 --no-display`: plain `ran 164 of 10865 tests (phases: 27; flavors: plain; display: excluded; crash tests: excluded)`, asan `ran 157 of 10865`, tsan `ran 157 of 10865`; gates 33/33; `ctest -L docs` 9/9; Clang and GCC, unity and non-unity, built clean.
