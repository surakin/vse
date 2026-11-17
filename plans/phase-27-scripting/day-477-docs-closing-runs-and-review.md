# Phase 27 / Day 477 — Docs, Closing Runs and the Phase Review

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 444-476.

Close the phase: documentation, the whole suite in every flavor, and the uncapped review.

## Tasks

- [ ] `docs/guide/scripting.md` complete: the model (partitions, the world view, command buffers, the sync points), a first script, the lifecycle, events, latent waits, tasks, the binding reference pointer, assets and hot reload, debugging, persistence and replay, the JIT table, the C API, the limits, the cost; **a page "Why your script runs on many threads, and what that means for you" for users coming from single-threaded engines**; `docs/reference/script-api.md` current; tagged samples compile and run.
- [ ] The Blueprint landing notes (Phase 32): which event-graph, latent, cast, interface and function-call semantics map to which scripting feature, and what does not (synchronous cross-entity calls, shared mutable state) with the recommended transpilation for each; written in `plans/phase-32-unreal-blueprint-import.md`.
- [ ] `PLAN.md` (roadmap row, progress log, assumption #13 amended with the final design, a new assumption for the multithreaded scripting model), `CLAUDE.md` (a "Scripting" bullet: partitions, command buffers, one owner per state, no blocking, tasks are pure, LuaJIT behind its gate), `ARCHITECTURE.md`, `BUILD.md`, `STATUS.md`, `KNOWN-ISSUES.md`, the memory note, all current.
- [ ] **The phase's last day runs the whole suite in every flavor:** `scripts/test-phase.sh --all --flavor all` (plus the `script_nojit` flavor), `ctest -L docs`, the demos the phase ships (`script_demo`, 1,000 frames with validation on Wayland and X11), on Linux; the Windows and macOS closing runs are Days 473-474's; record each `ran X of Y` line here.
- [ ] **The end-of-phase review is uncapped (CLAUDE.md):** `/code-review high --max-findings all` over the whole phase's diff (from the commit before Day 444 to the working tree), with extra scrutiny on the concurrency code (partitions, ownership token, command buffers, merge, events, tasks, hot reload, the debug pause) and the sandbox; verify, fix with regression tests shown to fail without the fix, or decline each finding with a reason; record the count and the outcome under "Post-implementation code review".
- [ ] `CURRENT_PHASE` stays 27 until Phase 28's first day (Day 478) changes it.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Phase 27 is documented, the whole suite passes in every flavor on Linux, the Windows and macOS days are recorded, and the uncapped review's findings are resolved.
