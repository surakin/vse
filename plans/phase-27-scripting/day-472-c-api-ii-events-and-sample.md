# Phase 27 / Day 472 — C API II: Events, Tasks and the Scripting Sample

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 471; Days 459-461.

Hosts exchange events with scripts and use the parallel machinery; a sample program shows it.

## Tasks

- [ ] `script_event_emit(world, name, value)` from the host and `script_event_subscribe(world, name, callback, user)` for events scripts emit; delivery at the sync point on the thread that runs `world_frame` (documented), in the bus order.
- [ ] `script_task_run` for host-started tasks over the task pool with a completion callback or a polled handle, so a host application (the host) can parallelize its own work through the engine's scheduler; the same pure-state rules apply.
- [ ] Host threads and worlds: several worlds on several host threads each running `world_frame` concurrently, with a shared task pool or one per world (a creation flag), tested under tsan with four host threads.
- [ ] The sample `tests/engine_library/script_sample.c`: a host creates a world, loads a script that spawns and moves 1,000 entities and reacts to a host event, calls back into a host function, runs 600 frames on N threads, prints the state hash; a ctest compares the hash across thread counts.
- [ ] `docs/guide/engine-library.md` gets the scripting section; the the host acceptance idea (scripts for room behavior) is written as a note, not a requirement.
- [ ] ABI snapshot and `abi_check` updated; shared build (`-DENGINE_BUILD_SHARED=ON`) tests pass.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A C host emits and receives script events, starts parallel tasks, runs multi-threaded script worlds, and a sample shows it with equal hashes at any thread count.
