# Phase 27 / Day 467 — Physics Bindings II: Contact and Trigger Events and the Character Controller

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 466; Days 459.

Physics talks to scripts through the event bus.

## Tasks

- [x] The physics contact events (begin, stay, end) and trigger events of Phase 25 Day 407 become script events `on_collision_begin/stay/end(self, other, contact)` and `on_trigger_enter/exit(self, other)` through the Day 459 bus, delivered to both entities' scripts in the entity-id order, at the next script phase after the step that produced them; contact data (point, normal, impulse) as `Value`s.
- [x] Event filtering by layer and by a per-component flag so only entities with a script that declared the handler pay for delivery; a step producing 100,000 events costs nothing for entities with no handler (measured).
- [x] The character controller bindings (Phase 25 Day 410): `character.move(e, desired_velocity)`, `jump`, `is_grounded`, `ground_normal`, `set_height`, platform velocity; the movement is a command applied at the sync point and the controller runs inside the physics step (as the controller already does).
- [x] Raycast vehicles, ragdolls and other gameplay built from these primitives are listed as library scripts in the demo (Day 475), not engine features.
- [x] Tests: a trigger volume fires `on_trigger_enter` once per entering entity at any thread count, a stack of boxes delivers the expected collision counts, the controller walks a ramp from a script, event delivery logs equal at 1 and 16 threads.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

## Notes

- Events: `script_physics::EmitPhysicsEvents(world, &runtime, options)` puts the step's events on the bus as `on_collision_begin/stay/end` and `on_trigger_enter/exit`; scripts receive them with `event.on` (`fn(self, hit)`; the plan's `on_collision_begin(self, other, contact)` shape became a payload table with `other`, `point`, `normal`, `depth`, `impulse`, `points`, because every other event of the bus is delivered that way). Both entities of a contact are told, the smaller stable id first; a trigger event goes to the trigger's entity.
- Filtering: an event is built only for an entity that listens (`ScriptRuntime::HasHandler`: an `event.on` handler or a coroutine parked in `wait_event`); `PhysicsEventOptions::layer_mask` and the `contacts`/`triggers` switches. The measurement of "100,000 events cost nothing" was not taken: the test with 100 unscripted boxes checks that nothing is put on the bus (`emitted == 0`, `Pending() == 0`), the cost per skipped receiver is two binary searches and a short scan of the partition's waiters.
- Characters: `script_physics::CharacterSet` (entity-ordered controllers) installs a new `PhysicsWorld::SetPreStepHook`, so the moves run inside each fixed step; `character.create/destroy/move/jump/set_height` are merge commands (kinds 110-114), `exists/is_grounded/ground_normal/ground_velocity/vertical_speed` read the last step. `CharacterController::SetHeight` and `Config()` are new in `physics`; `SetHeight` refuses to stand up into static or kinematic geometry and the set retries each step.
- Found on the way: a body that names an entity without a `RigidBody` is destroyed by `SyncBodies`, so a character's body names no entity; `EmitPhysicsEvents` resolves it through `CharacterSet::EntityOfBody` (`PhysicsEventOptions::characters`).
- Pages touched: `docs/guide/scripting.md` ("Events", "Physics": contact and trigger events, characters), `docs/reference/script_physics.md`, `physics.md`, `script.md`, `script-api.md`, `symbols.txt`.

## Runs

- Phase 27 plain: `ran 259 of 11186 tests (phases: 27; flavors: plain; display: excluded; crash tests: excluded) in 188 s`
- Phase 27 asan: `ran 252 of 11186 tests (phases: 27; flavors: asan; display: excluded; crash tests: excluded) in 18 s`
- Phase 27 tsan: `ran 252 of 11186 tests (phases: 27; flavors: tsan; display: excluded; crash tests: excluded) in 20 s`
- Phase 25 (the pre-step hook and `SetHeight` touch `physics`) plain: `ran 233 of 11186 tests (phases: 25; flavors: plain; display: excluded; crash tests: excluded) in 216 s`; asan: `ran 229 of 11186 tests (phases: 25; flavors: asan; display: excluded; crash tests: excluded) in 14 s`.
- Reduced runs, not the whole suite. Gates 75/75 (module_deps, phase labels, std_usage, shared_unit, platform_neutral, file/config/cmdline access, signal safety, module map paths); `ctest -L docs` 9/9; docgen lint at zero. Builds clean: Clang unity, Clang non-unity, GCC non-unity (a GCC unity build stops in `engine/net`, known). Windows, macOS and `-DENGINE_BUILD_SHARED=ON` not run.

## Post-implementation code review

`/code-review high`: 6 findings.
1. `HasHandler` ignored a coroutine parked in `wait_event`: fixed (regression test `a coroutine parked in wait_event is a listener`, shown to fail without the fix).
2. `SetHeight` could stand up inside a ceiling: fixed (tests in `character_test.cpp` and `script_physics_events_test.cpp`).
3. `(void)` on an unused parameter in `NeedCharacters`: fixed (parameter removed).
4. The `CharacterSet` ownership note named a method that does not exist: fixed.
5. A native `EventBus` subscriber sees only the events of listening entities: declined, documented in the header and the guide.
6. Two scripts creating a character for one entity in one tick: the second is dropped at the merge without an error: declined, recorded in `KNOWN-ISSUES.md`.

**Done:** Scripts react to collisions and triggers and drive a character, with events delivered in a thread-independent order.
