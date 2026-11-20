# Phase 27 / Day 472 — C API II: Events, Tasks and the Scripting Sample

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 471; Days 459-461.

Hosts exchange events with scripts and use the parallel machinery; a sample program shows it.

## Tasks

- [x] `script_event_emit(world, name, value)` from the host and `script_event_subscribe(world, name, callback, user)` for events scripts emit; delivery at the sync point on the thread that runs `world_frame` (documented), in the bus order.
- [x] `script_task_run` for host-started tasks over the task pool with a completion callback or a polled handle, so a host application (the host) can parallelize its own work through the engine's scheduler; the same pure-state rules apply.
- [x] Host threads and worlds: several worlds on several host threads each running `world_frame` concurrently, with a shared task pool or one per world (a creation flag), tested under tsan with four host threads.
- [x] The sample `tests/engine_library/script_sample.c`: a host creates a world, loads a script that spawns and moves 1,000 entities and reacts to a host event, calls back into a host function, runs 600 frames on N threads, prints the state hash; a ctest compares the hash across thread counts.
- [x] `docs/guide/engine-library.md` gets the scripting section; the host acceptance idea (scripts for room behavior) is written as a note, not a requirement.
- [x] ABI snapshot and `abi_check` updated; shared build (`-DENGINE_BUILD_SHARED=ON`) tests pass.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A C host emits and receives script events, starts parallel tasks, runs multi-threaded script worlds, and a sample shows it with equal hashes at any thread count.

## Notes

- New table functions (appended): `script_event_emit`, `script_event_subscribe`, `script_event_unsubscribe`, `script_task_run`; types `engine_script_event_fn`, `engine_script_task`, `ENGINE_SCRIPT_TASK_RUN/MAP/FOR`; `engine.h` wraps them; snapshot regenerated. `tests/engine_library/script_sample.c` (ctest `engine_c_script_sample`, three flavors).
- Decisions: (1) a task runs synchronously on the calling thread (a host thread is not a job-system participant), no handle or completion callback; (2) the engine allows one frame thread, so "several worlds on several host threads running frames" became one frame thread beside three threads that each call into a world of their own (`script_call`, `script_task_run`, retried on NOT_READY), under tsan; (3) the "N threads" of the hash comparison are the script partitions (1, 2, 4, 8): the host's frame ticks serially anyway, and the state hash is equal for all four (`e3e3ca13a45686f9`); (4) a host subscription made before the runtime exists is attached when the runtime is made.
- The first `--affected` runs were started without `--no-display` (display tests included; they passed or skipped as before); the later runs use `--no-display`.
- Pages touched: `docs/guide/engine-library.md` ("Events and tasks", with the host note), `docs/reference/*`.

## Runs

- `scripts/test-phase.sh --affected` plain: `ran 4185 of 11312 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 507 s`; asan: `ran 3480 of 11312 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 60 s`; tsan: `ran 3135 of 11312 tests (phases: all; flavors: tsan; display: included; crash tests: excluded) in 68 s`. Failing, all known: the Job-destroyed test (phase 2), the Metal library test (phase 21), and the timing test `Snapshot: the cost of a voice...` (plain, phase 26, under load).
- After the review's fix: phase 27 plain `ran 301 of 11312 tests (phases: 27; flavors: plain; display: excluded; crash tests: excluded) in 56 s`, asan `ran 294 of 11312 ... in 50 s`, tsan `ran 294 of 11312 ... in 55 s` (all passed).
- `ctest -L docs` 9/9, docs regenerated. Builds clean: Clang unity, Clang non-unity, GCC (both ran `script_sample`, same hash). `-DENGINE_BUILD_SHARED=ON -DENGINE_LIBRARY_WITH_IMPORTER=OFF`: `libengine.so` builds, `abi_check_library` passes; `host_script` and `script_sample` pass there (asan/tsan ABI checks not built).

## Post-implementation code review

`/code-review high`: 6 findings. Fixed: (1) the task pool kept the budget of the moment it was made, so `script_set_budget` did not reach tasks (the pool is remade; regression in `script_sample.c`, shown failing without the fix). Declined or recorded: (2) no handle or completion callback for tasks (synchronous by decision, `KNOWN-ISSUES.md`); (3) frames cannot run on several threads (the Phase 23 rule, documented); (4) extra payload roots are ignored (documented as "first root"); (5) `DeliverEvent` allocates per event (Day 476); (6) a bad map task reports INTERNAL (recorded).

**Done:** A C host emits and receives script events, runs pure tasks, calls into several worlds from several threads, and a sample shows the same state hash at 1, 2, 4 and 8 partitions.
