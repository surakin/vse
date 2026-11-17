# Phase 27 / Day 455 — ECS Bindings I: Entities and Transforms

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 450, 452; Phase 17.

The definition of done's first half: a script spawns and moves an entity.

## Tasks

- [ ] `entity.spawn(prefab_or_nil)`, `entity.destroy(e)`, `entity.valid(e)`, `entity.name(e)`, `entity.find(name)`; spawn and destroy are `world-write` through the command buffer with reserved handles (Day 452).
- [ ] `transform.get(e)`, `transform.set_position/rotation/scale(e, ...)`, `transform.world(e)`, `transform.translate(e, v)`, parenting (`entity.set_parent`), all against the Phase 17 components; own-entity writes visible at once through the overlay, other entities' reads from the tick-start view.
- [ ] The script's own entity as `self` in every callback; the script cannot obtain another entity's *script state* (only the ECS components), so there is nothing shared between states.
- [ ] The definition-of-done test: a Lua script spawns a cube entity and moves it along a path for 120 ticks on 1 and 8 threads; the final transforms are equal; the entity is drawn by the render sync in the offscreen test.
- [ ] Bindings are marked and documented; the ECS archetype changes made by the merge use the Phase 17 batched structural path (one pass per component type, not one per command).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A Lua script spawns, moves, parents and destroys entities, identically on any thread count.
