# Phase 27 / Day 467 — Physics Bindings II: Contact and Trigger Events and the Character Controller

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 466; Days 459.

Physics talks to scripts through the event bus.

## Tasks

- [ ] The physics contact events (begin, stay, end) and trigger events of Phase 25 Day 407 become script events `on_collision_begin/stay/end(self, other, contact)` and `on_trigger_enter/exit(self, other)` through the Day 459 bus, delivered to both entities' scripts in the entity-id order, at the next script phase after the step that produced them; contact data (point, normal, impulse) as `Value`s.
- [ ] Event filtering by layer and by a per-component flag so only entities with a script that declared the handler pay for delivery; a step producing 100,000 events costs nothing for entities with no handler (measured).
- [ ] The character controller bindings (Phase 25 Day 410): `character.move(e, desired_velocity)`, `jump`, `is_grounded`, `ground_normal`, `set_height`, platform velocity; the movement is a command applied at the sync point and the controller runs inside the physics step (as the controller already does).
- [ ] Raycast vehicles, ragdolls and other gameplay built from these primitives are listed as library scripts in the demo (Day 475), not engine features.
- [ ] Tests: a trigger volume fires `on_trigger_enter` once per entering entity at any thread count, a stack of boxes delivers the expected collision counts, the controller walks a ramp from a script, event delivery logs equal at 1 and 16 threads.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Scripts react to collisions and triggers and drive a character, with events delivered in a thread-independent order.
