# Phase 25 / Day 405 — Events, ECS Integration and Debug Draw

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 404; Phase 17, Phase 5 (renderer lines).

What gameplay and tools see of the simulation.

## Tasks

- [x] Contact events: begin, stay (optional, off by default) and end per collider pair with the manifold summary (point count, deepest point, normal, total normal impulse) and the children's indices; trigger enter and exit; joint break events; the events are written to a per-step ring in sorted order and read through an `EventReader` after the step (the ECS event pattern of Phase 17 if one exists, else the world's own ring with a documented lifetime of one update).
- [x] Collision responses to the ECS: a `PhysicsContacts` view for a system that wants to read pairs; the write-back of `Transform` (position, rotation, interpolated) for dynamic bodies and the read of `Transform` for kinematic ones, with the dirty-flag discipline of the ECS's change detection (a body that did not move does not mark its transform changed).
- [x] Entity destruction: destroying an entity removes its body, its joints (the partner wakes) and cancels its events; destroying a world frees every body.
- [x] Debug draw: shapes, contact points and normals, joint frames and limits, island colours and sleeping state, broad-phase boxes, drawn through the renderer's line path (Phase 5/14 debug lines) behind `physics.debug_draw` (a setting); the draw reads the world as of the last step and allocates nothing in a frame after warm-up.
- [x] Stats surface: `PhysicsStats` readable per world and through the settings/diagnostics hooks the engine already has.
- [x] Tests: event ordering and counts for a ball bouncing three times; trigger enter/exit; joint-break event; a destroyed entity cancels pending events; the debug draw produces the expected line counts for a known scene and allocates nothing per frame (the engine's allocation counters); the transform dirty flags.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 410-411, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Gameplay reads contact, trigger and break events in a fixed order, entities and bodies stay in step, and the simulation can be drawn for debugging.

## Outcome

`physics/events.h/.cpp`: `PhysicsWorld::Events()` with contact begin/end (stay with `StepConfig::stay_events`, setting `physics.stay_events`) carrying bodies, entities, point count, deepest point, normal, depth and the solver's normal impulse (`ContactSolver::PairImpulse`), exact trigger enter/exit (narrow-phase test, resting pairs carry their state), joint break events with the last force and torque; lists in step order and collider-key order, kept across the steps of an `Update`; resting pairs that fall asleep do not end; `DestroyBody` cancels the events that name it; `Contacts()` is the view of touching pairs; `AllStats()` gathers the counters. `physics/joint_sync.h/.cpp`: the ECS `Joint` component (deferred from Day 398), `JointLink` and `SyncJoints`. `physics/debug_draw.h/.cpp`: `DebugDraw` appends `spatial::DebugLine` segments (shapes, contacts, joints, island colors, boxes) and allocates nothing once the vector has grown; setting `physics.debug_draw`. The transform write-back stamps a chunk only when a pose differs (`LazyWrite`); `DriveKinematicBodies` moves kinematic bodies to their entity's transform. 12 new test cases (`tests/physics/events_test.cpp`), guide section. Plain ran 3586 of 9618 tests (after the review fixes); before them asan ran 2965 and tsan 2553. Built and run on 4 trees.
Not done: the debug lines are not drawn through the renderer (the module holds no renderer type; the program makes the line pass), the diagnostics hooks beyond `AllStats`, a hull, mesh or compound outline (drawn as its local box).

## Post-implementation code review

7 findings. Fixed: no kinematic transform read (added `DriveKinematicBodies` and a test); the joint report's `broken` doc; a GCC-only failure of the Day 403 ray oracle (grazing rays are excluded). Declined: contact events cost every step with no off switch (not measured); a destroyed body's pairs end without an end or exit event (the plan's wording); hull, mesh and compound drawn as boxes.
