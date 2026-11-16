# Phase 25 / Day 393 — Events, ECS Integration and Debug Draw

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 392; Phase 17, Phase 5 (renderer lines).

What gameplay and tools see of the simulation.

## Tasks

- [ ] Contact events: begin, stay (optional, off by default) and end per collider pair with the manifold summary (point count, deepest point, normal, total normal impulse) and the children's indices; trigger enter and exit; joint break events; the events are written to a per-step ring in sorted order and read through an `EventReader` after the step (the ECS event pattern of Phase 17 if one exists, else the world's own ring with a documented lifetime of one update).
- [ ] Collision responses to the ECS: a `PhysicsContacts` view for a system that wants to read pairs; the write-back of `Transform` (position, rotation, interpolated) for dynamic bodies and the read of `Transform` for kinematic ones, with the dirty-flag discipline of the ECS's change detection (a body that did not move does not mark its transform changed).
- [ ] Entity destruction: destroying an entity removes its body, its joints (the partner wakes) and cancels its events; destroying a world frees every body.
- [ ] Debug draw: shapes, contact points and normals, joint frames and limits, island colours and sleeping state, broad-phase boxes, drawn through the renderer's line path (Phase 5/14 debug lines) behind `physics.debug_draw` (a setting); the draw reads the world as of the last step and allocates nothing in a frame after warm-up.
- [ ] Stats surface: `PhysicsStats` readable per world and through the settings/diagnostics hooks the engine already has.
- [ ] Tests: event ordering and counts for a ball bouncing three times; trigger enter/exit; joint-break event; a destroyed entity cancels pending events; the debug draw produces the expected line counts for a known scene and allocates nothing per frame (the engine's allocation counters); the transform dirty flags.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Gameplay reads contact, trigger and break events in a fixed order, entities and bodies stay in step, and the simulation can be drawn for debugging.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
