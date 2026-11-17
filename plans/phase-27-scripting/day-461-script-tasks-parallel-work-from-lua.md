# Phase 27 / Day 461 — Script Tasks: Parallel Work Started from Lua

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 446, 449-451, 460; Phase 2.

Users can start parallel work themselves, not only have their entities' scripts run in parallel: pathfinding, procedural generation, AI evaluation, anything pure.

## Tasks

- [ ] `task.run(fn_or_script_asset, args) -> handle`: runs `fn` on a **task state** from a pool (a state with only the `pure` bindings: math, random-from-seed, string and table libraries and the `Value` types; no world, no events, no settings), with the arguments copied in as `Value`s and the result copied out; the function is passed as a minted script asset id or as a source string that is hashed and cached (a Lua closure cannot cross states: the docs say so and the error says so).
- [ ] `task.parallel_for(count, fn_asset, args, chunk)` and `task.map(array, fn_asset, chunk)` split work into chunks on the job scheduler; results come back in index order; a chunk that fails reports the index and the error; the budget of Day 449 applies per chunk.
- [ ] Results arrive through latent waits: `local r = wait_task(handle)` (or `task.result(handle)` after an `on_task_done` event); delivery at the sync point in handle order, so the order is the same on any thread count.
- [ ] The task state pool: `script.task_states` (default: one per worker), reused between tasks, reset on a budget hit or a memory hit, created lazily; a task cannot see another task's globals (reset or per-task environment).
- [ ] Back-pressure and limits: a cap on queued tasks and on the bytes of arguments (settings), `task.run` returns an error value when full; tasks are cancelled with their owner entity and never deliver to a destroyed one.
- [ ] Determinism: a task's result depends only on its function and arguments (the seed is an argument), so the world state stays identical for any thread count; a test runs a world whose entities start tasks on 1 and 16 threads; a planted attempt to reach world state from a task is a clear error.
- [ ] Tests: single tasks, `parallel_for` over 1,000,000 elements against the serial loop (equal results, speedup recorded), cancellation, errors, pool exhaustion; tsan clean.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A script starts parallel computations that run on every core, return their results deterministically, and cannot touch the world.
