# Phase 27 / Day 475 — A Scripted Gameplay Demo: Physics, Audio and Many Threads

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 458-468; the `physics_demo`.

The demo that shows the point of the phase: a game written in Lua that uses every core.

## Tasks

- [ ] `game/script_demo` (C++ host, engine modules directly, like `physics_demo`): a floor, spawner scripts, a player character driven by a script, trigger volumes, pickups that play sounds, and a few thousand scripted entities each with a small behavior (wander, flee, flock) in Lua; scripts live in `game/script_demo/scripts/` and are minted by `asset_mint script` in the build.
- [ ] Options `--threads N`, `--partitions P`, `--entities N`, `--screenshot`, `--frames`; the on-screen overlay shows the script time per frame, the partitions' load, the thread count and the hot scripts (Phase 14 text).
- [ ] A parallel task showcase: a script calls `task.parallel_for` for a flow-field or noise computation and the result appears in the world; the overlay shows the speedup against `--threads 1`.
- [ ] Hot reload live: editing a script file changes the running demo; the ctest runs the offscreen path with hot reload driven from a script that rewrites a file.
- [ ] The ctest `script_demo_screenshot` (label `gpu;demo`, skip 77 without a GPU) and a headless hash test that runs the demo world at 1 and 16 threads and compares hashes.
- [ ] README, the guide's tutorial page (`docs/guide/scripting.md`, "Your first script" and "Using every core"), the user watches the demo (recorded in `KNOWN-ISSUES.md` if not yet done).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A scripted game runs in a window on all cores, matches its single-thread run bit for bit, and is documented as a tutorial.
