# Phase 27 / Day 458 — The Parallel Script System on the Job Scheduler

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 451-452, 457; Phase 2.

**The headline day:** the partitions run at the same time on every core, and the result does not depend on how many there are.

## Tasks

- [ ] The script system runs each phase as one job per non-empty partition on the Phase 2 scheduler (`ParallelFor` over partitions with the work-stealing scheduler balancing uneven partitions); the main thread helps while waiting; the phases are separated by the sync points (apply buffers, deliver events, deliver deferred destroys).
- [ ] Load balancing: partitions with many scripted entities are split by a *stable* sub-assignment only if the spike showed a need (a heavy partition is a user-visible performance cliff: the `script.partitions` default and the distribution of the hash are tested for evenness on 1,000, 100,000 and 1,000,000 entity ids, results recorded).
- [ ] The thread-count independence suite: the Day 452 world, the Day 455 definition-of-done script and a 100,000-entity gameplay scene run at 1, 2, 3, 7, 16 threads; every ECS state hash, event log and script log equal; the whole suite runs under tsan with the scheduler's worker count set by `ENGINE_JOB_THREADS`.
- [ ] No blocking inside a partition job: a binding that would block (file, network, wait) is `main-thread-only` or latent; a debug check fails a partition job that runs longer than a setting without yielding (reported as a hot script with its name).
- [ ] Scheduling with the rest of the frame: the script phases are separate `SystemDesc`s ordered against physics and rendering by their declared reads and writes, so the Phase 17 scheduler can overlap script work with systems that do not conflict (e.g. audio mixing, culling); a test shows the overlap on a scene.
- [ ] The scaling curve (entities per second against threads, `P` 16/64/256) recorded in the guide ("Cost"), the Day 444 spike's numbers re-measured on the real system and compared.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Scripted gameplay runs on all cores, scales with the thread count, and produces bit-identical world state at any thread count.
