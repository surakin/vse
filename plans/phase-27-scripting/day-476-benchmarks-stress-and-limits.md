# Phase 27 / Day 476 — Benchmarks, Stress and the Limits

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 458-475.

Numbers for what scripts cost and how they scale, and the system under hostile load.

## Tasks

- [ ] `script_bench` (Release, run by hand): call overhead Lua-to-C and C-to-Lua, a tick with 10,000, 100,000 and 1,000,000 scripted entities at 1, 2, 4, 8, 16 threads and `P` = 16/64/256/1024, command-buffer and merge costs, event throughput, coroutine wake cost, task throughput and `parallel_for` speedups, memory per state, per instance, per coroutine; the scaling curves and the recommended `P` recorded in `docs/guide/scripting.md` ("Cost") and the day file.
- [ ] A stress test (a ctest `script_stress_large`, Release and RelWithDebInfo only, like the physics one): one million scripted entities for 600 ticks with spawning, destroying, events and tasks, state checked by hash against a serial run at a smaller scale and between thread counts.
- [ ] The hostile-script suite as one run: every case of the corpus in 64 partitions at once for 60 s on 16 threads under asan and tsan: no leak, no race, no hang, every case ends with a status.
- [ ] Limits written down: the caps (partitions, memory, coroutines, tasks, event chain, value size) and what happens at each; the limits table in the guide and `KNOWN-ISSUES.md` for anything that is a limitation by design (no shared Lua state between entities; no synchronous cross-entity calls; no script-visible threads or locks).
- [ ] A regression threshold file for the benchmark (generous bounds, only in Release, like the physics bench) so a 3x slowdown fails a ctest.
- [ ] Anything the benchmarks showed that the plan must change (the default `P`, the hash, the command format) is fixed or filed.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The cost and scaling of scripting are measured and recorded, the stress and hostile suites pass in every flavor, and the limits are written down.
