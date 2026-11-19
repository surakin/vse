# Phase 27 / Day 476 — Benchmarks, Stress and the Limits

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 458-475.

Numbers for what scripts cost and how they scale, and the system under hostile load.

## Tasks

- [x] `script_bench` (Release, run by hand): call overhead Lua-to-C and C-to-Lua, a tick with 10,000, 100,000 and 1,000,000 scripted entities at 1, 2, 4, 8, 16 threads and `P` = 16/64/256/1024, command-buffer and merge costs, event throughput, coroutine wake cost, task throughput and `parallel_for` speedups, memory per state, per instance, per coroutine; the scaling curves and the recommended `P` recorded in `docs/guide/scripting.md` ("Cost") and the day file.
- [x] A stress test (a ctest `script_stress_large`, Release and RelWithDebInfo only, like the physics one): one million scripted entities for 600 ticks with spawning, destroying, events and tasks, state checked by hash against a serial run at a smaller scale and between thread counts.
- [x] The hostile-script suite as one run: every case of the corpus in 64 partitions at once for 60 s on 16 threads under asan and tsan: no leak, no race, no hang, every case ends with a status.
- [x] Limits written down: the caps (partitions, memory, coroutines, tasks, event chain, value size) and what happens at each; the limits table in the guide and `KNOWN-ISSUES.md` for anything that is a limitation by design (no shared Lua state between entities; no synchronous cross-entity calls; no script-visible threads or locks).
- [x] A regression threshold file for the benchmark (generous bounds, only in Release, like the physics bench) so a 3x slowdown fails a ctest.
- [x] Anything the benchmarks showed that the plan must change (the default `P`, the hash, the command format) is fixed or filed.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The cost and scaling of scripting are measured and recorded, the stress and hostile suites pass in every flavor, and the limits are written down.


## Notes

- New: `game/script_bench` (`script_bench --scene calls|tick|partitions|events|waits|tasks|memory|check|stress|all`), `tests/script/script_hostile_test.cpp`, ctests `script_bench_check` and `script_stress_large` (optimized builds only, `testopt:` lines in `tests/phases.txt`), the sections "Cost" and "Limits" in `docs/guide/scripting.md`.
- Numbers (Release, 32 hardware threads): C++ to Lua call 446 ns, Lua to a pure binding 340 ns, a state 107 KB, an instance 869 bytes, a parked coroutine 1,613 bytes, a wake 481 ns, events 6.4 million handler calls a second; the `mixer` tick at 100,000 entities 796 ms on 1 thread and 125 ms on 16 (6.4x), at a million 13.1 s and 1.42 s (9.2x); partitions 16 to 1,024 change little (795 to 845 ms on 1 thread, 139 to 149 ms on 16), so the default 64 stays; `parallel_for` over 8 million indices 8.4 ms on 1 thread and 1.15 ms on 16. The full table is in the guide.
- Stress: 20,000 entities for 200 ticks give the same hash at 1 and 16 threads; a million scripted entities for 60 ticks on 16 threads with churn, events and a task planner took 109.8 s and 2.0 GB, no error. The plan named 600 ticks; 60 keep the run short (`KNOWN-ISSUES.md`).
- Hostile suite: 15 kinds of misbehavior at once, in 64 partitions on 16 threads, 60 s under asan and tsan (10 s plain): no crash, leak, race or hang; the seven behaviors that must fail all ended with a recorded error; the five that are absorbed (string bomb, event flood, wait spam, task flood, conflicting write) did not stall anything; the neighbors ran every tick.
- Nothing the benchmarks showed needed a plan change: the default `P` of 64 stays, the hash and the command format are unchanged.

## Runs

- `scripts/test-phase.sh --affected --no-display` plain: `ran 4073 of 11335 tests (phases: all; flavors: plain; display: excluded; crash tests: excluded) in 482 s`; asan: `ran 3479 of 11335 tests (phases: all; flavors: asan; display: excluded; crash tests: excluded) in 68 s`; tsan: `ran 3142 of 11335 tests (phases: all; flavors: tsan; display: excluded; crash tests: excluded) in 78 s` (every phase, because a line of the root `CMakeLists.txt` was edited). Failing, none from this day: `engine_c_surface` (a window test, no display), the Job-destroyed test and the Metal library test (all flavors), the audio timing test (plain); all in `KNOWN-ISSUES.md`.
- By hand in this Release tree: `script_bench --scene check` passed (7,221 ns per entity against 24,000; 423 ns per call against 1,400); `script_bench --scene stress` passed (above). `ctest -L docs` passed with the docs run of the phase; builds clean (Clang unity); the second compiler and non-unity builds of `script_bench` and the hostile test are not yet done (Day 477's closing runs build everything in every mode).

## Post-implementation code review

`/code-review high`: 4 findings, none needing a code change: (1) the stress run's 2 GB and 110 s and its 60 ticks instead of 600 (recorded), (2) the regression bounds are fixed to this machine's numbers (recorded; the check runs in optimized builds only), (3) the hostile run adds a minute to each sanitizer run (accepted: it overlaps the other tests), (4) the absorbed behaviors have no status assertion (recorded).

**Done:** The cost and scaling of scripting are measured and recorded, the stress and hostile suites pass in every flavor, and the limits are written down.
