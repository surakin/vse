# Phase 27 / Day 457 — The Script Component, the Lifecycle and the Script System

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 451-456.

How a script is attached to an entity and called: first correct and serial (one thread), then Day 458 runs it in parallel.

## Tasks

- [ ] `ScriptComponent` (script asset id or source name, instance parameters as `Value`s, enabled flag); the instance's Lua state lives in the entity's partition, created on the first tick after the entity appears.
- [ ] Lifecycle callbacks the script defines on its table: `on_create(self)`, `on_start(self)`, `on_update(self, dt)`, `on_fixed_update(self, dt)` (called per physics step count the tick holds), `on_destroy(self)`, `on_enable`/`on_disable`; the order within a partition is entity stable id order, across partitions undefined by design and never observable (Day 452).
- [ ] `script::MakeScriptSystem()` as an `ecs::SystemDesc` that runs the phases in order (create/start, fixed updates, updates, the sync point, destroys); this day runs the partitions serially in one loop; its declared reads and writes (`ReadsAllComponents`, `WritesResource<ScriptWorld>`) keep the Phase 17 scheduler honest.
- [ ] Script errors: a failing callback disables that instance (setting `script.on_error`: `disable` default, `retry`, `abort_in_debug`), logs the trace once, and never stops the world; a destroyed entity's instance is closed without a callback if `on_destroy` itself fails.
- [ ] Per-instance state: a Lua table `self` per instance, private to the partition's state; the instance parameters and the persistent subset (Day 469) are `Value`s the engine can copy.
- [ ] Tests: lifecycle order, disabled-on-error, enable and disable, destroy during a tick, 10,000 instances.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Entities run their scripts' lifecycle callbacks in a defined order from one script system, serially and correctly.
