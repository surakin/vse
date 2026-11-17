# Phase 27 / Day 464 — Hot Reload

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 457-458, 460, 463.

Edit a script and the running game picks it up, keeping what it can; development only.

## Tasks

- [ ] A watcher (through the file layer and the dev loader) notices a changed source, recompiles it in a scratch state first (a failed compile reports file, line and message and **keeps the old version running**), then stages the new version.
- [ ] The swap happens per partition at the sync point, never during a tick: for each instance of the script, the state is carried over by the script's own hooks (`on_reload_save(self) -> table` and `on_reload_restore(self, saved)`; without hooks the instance's persistent subset of `self` (Day 469) is kept and the callbacks are rebound), cancelled coroutines get `on_cancel`; all partitions swap in the same sync point so a tick never mixes versions.
- [ ] Pending events and tasks started by the old version still deliver (their handlers rebind by name); a handler that no longer exists is dropped with a log line.
- [ ] Reload of a dependency reloads its dependents in dependency order; a reload loop guard (rate limit) so an editor saving twice does not reload twice in a tick.
- [ ] Thread rules: the reload runs on the main thread between ticks; partitions are idle at that point (the ownership token check proves it); tsan clean while ticks run on 16 threads and reloads arrive.
- [ ] Tests: reload with state kept, reload with a syntax error (old version continues), reload during waits, reload of a dependency, 100 reloads in a row under load; the reload is a no-op and absent when `script.hot_reload` is off (default off in a shipped build).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A saved script change takes effect in the running world at a sync point on every thread, keeps state through hooks, and a broken edit leaves the old script running.
