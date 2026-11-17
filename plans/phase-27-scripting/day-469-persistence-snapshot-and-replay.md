# Phase 27 / Day 469 — Persistence, Snapshots and Replay

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 457-458, 465-468; Phase 19.

Scripted game state can be saved, restored and replayed exactly, on any thread count.

## Tasks

- [ ] The **persistent subset** of an instance's `self`: values the script marks with `persist("hp", "inventory")` (numbers, strings, booleans, vectors, entity handles remapped through the storage id mapping, tables of those with the `Value` limits); everything else (closures, userdata, coroutines) is by design not saved and the docs say what happens to it (a script rebuilds it in `on_load`).
- [ ] Saving: `ScriptComponent` and the persistent values are a registered component of `engine/storage` (full and incremental saves, versioned, hostile-input rules); running coroutines are saved as *restart requests* by name where a script opts in (`persist_coroutine`), otherwise cancelled with `on_cancel` at save time.
- [ ] Loading: instances are created in their partitions, `on_load(self)` runs before `on_start`; script definitions changed between save and load go through the component's version upgrade hooks.
- [ ] The world snapshot with scripts: the ECS hash, the script worlds' persistent state hash, the event queues and the task results, restored atomically; a recorded **replay log** (input snapshots per tick, script random seeds, task results are recomputed) replays a session to the same hash at the end.
- [ ] Replay across thread counts: a session recorded at 16 threads replays at 1 thread to the same hash, and the reverse; snapshot at tick N, restore on a different thread count, run on, same hash as the uninterrupted run.
- [ ] Tests: a gameplay scene with 10,000 scripted entities saved and loaded, the replay matrix over thread counts, corrupt save files refused by value, version upgrade of a script component.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Scripted state saves, restores and replays to bit-identical hashes regardless of the thread count.
