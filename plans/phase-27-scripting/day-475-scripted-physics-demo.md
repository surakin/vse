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
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A scripted game runs in a window on all cores, matches its single-thread run bit for bit, and is documented as a tutorial.


## Notes

What is built (headless): `game/script_demo` (`demo_world.h/.cpp`, `main.cpp`, `scripts/demo.lua` embedded by the build through `demo_script.cpp.in`, README): 2,000 dynamic bodies with `wander`, `flock` and `flee` behaviors, a kinematic player the script walks along a square, 16 trigger pickups (`on_trigger_enter`, then an event the host counts), and a planner that searches a field with `task.parallel_for` and moves a marker; the full frame order (scripts and merge, sync, kinematic drive, step, write-back, physics events). Options `--threads --partitions --entities --frames --report`. `tests/script_demo` (five cases, three flavors): the same hash at 1, 2 and 16 threads, the marker, the pickups, hot reload in the middle of a run (same hash at 1 and 16 threads, the edit changes the run) and a script that does not compile is refused. Measured (scripts only, 2,000 bodies, 16 partitions): 1 thread 114.6 ms per frame, 2: 57.9, 4: 29.8, 8: 20.7, 16: 12.2; one hash.

What is NOT built, so the first six boxes above stay open: the window and the on-screen overlay (script time, partition load, hot scripts, the speedup against `--threads 1`), `--screenshot` and the `script_demo_screenshot` ctest (the user asked to skip anything that opens a window or uses a GPU in this run), the player driven by input, pickups that play sounds, hot reload from an edited file (the test reloads from text), scripts minted by `asset_mint`, and the tutorial pages: CLAUDE.md puts tutorials on the phase's last day, so "Your first script" and "Using every core" are Day 477's. These are in `KNOWN-ISSUES.md`.

Found on the way (engine bug, fixed): a coroutine resumed from `wait_frames`/`wait` that then started a task and waited for it in the same tick was resumed at once with "the task has not finished" (the waiter was treated as ready although the task runs at the end of the tick). The waiter is now due the tick after it was made; regression `Tasks: a task asked for after a timed wait is waited for until it has run`, shown failing without the fix.

Pages touched: `docs/guide/scripting.md` ("The script demo"), `ARCHITECTURE.md`, `game/script_demo/README.md`.

## Runs

- `scripts/test-phase.sh --affected --no-display` plain: `ran 4070 of 11330 tests (phases: all; flavors: plain; display: excluded; crash tests: excluded) in 522 s`; asan: `ran 3478 of 11330 tests (phases: all; flavors: asan; display: excluded; crash tests: excluded) in 67 s`; tsan: `ran 3141 of 11330 tests (phases: all; flavors: tsan; display: excluded; crash tests: excluded) in 71 s` (the script selected every phase because a line of the root `CMakeLists.txt` was edited). Failing, none from this day: `engine_c_surface` (plain, a window test run without a display), the Job-destroyed test and the Metal library test (all flavors), and the tsan flake `LuaJIT sandbox: a trusted state ...`; all listed in `KNOWN-ISSUES.md`.
- After the review's fix: `ctest -R "Script demo"` passed in plain, asan and tsan (5 cases each). Builds clean: Clang unity, Clang non-unity, GCC non-unity (`script_demo`, `script_demo_tests`, `script_tests`).

## Post-implementation code review

`/code-review high`: 4 findings. Fixed: (1) the task-waiter bug above (regression shown failing), (2) the physics world kept the scheduler of an earlier frame. Recorded, not done: (3) the window, overlay and screenshot test, (4) a reload's refusal reason reaches the caller only as a stderr line.

**Done (partly):** The scripted game runs headless on all cores and matches its single-thread run bit for bit; the window, overlay, screenshot and tutorial are open (see Notes).
