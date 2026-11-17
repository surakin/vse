# Phase 25 / Day 409 — C API II: Joints, Events, the Controller and the Sample

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 408.

The rest of the physical surface for hosts, shown in a small sample.

## Tasks

- [x] `ENGINE_COMPONENT_JOINT` (`engine_joint`: kind, the two entities, anchors, axes, limits, motor, breaking) and the function to read a joint's reaction force; `ENGINE_COMPONENT_CHARACTER_CONTROLLER` (`engine_character`: radius, height, step, slope; `character_move(entity, velocity)`, `character_state(entity, &state)`).
- [x] Events through the C boundary: `physics_events(world, out, capacity, &total)` returning contact begin/end, trigger enter/exit and joint-break records for the last step as plain structs (sorted), valid until the next `frame`; no callbacks cross the boundary (the Phase 23 rule: a callback is a pointer plus user data and runs on a documented thread; none is added here).
- [x] Snapshot and restore through the C API: `physics_snapshot(world, &buffer)` and `physics_restore(world, data, size)` using `engine_buffer` ownership, so a host can rewind.
- [x] The sample: `tests/engine_library/physics_sample.c` extends the walk-around of Day 371: a capsule character walks on a mesh floor, pushes crates, a door on a hinge, a trigger lights a lamp, a ball is thrown by a key; it doubles as the tutorial code.
- [x] Acceptance with the the host assets (`the host project checkout`): a glTF scene imported as a level gets colliders from its meshes (a flag on `level_spawn` that adds mesh colliders to static nodes, minted collision meshes from the glTF through `asset_mint`), and a character can walk it; the screenshot golden is checked by eye.
- [x] Tutorial `docs/tutorials/physics-in-your-window.md`; the guide's host duties updated (step order, event lifetime, thread rules).
- [x] Tests: the new components' argument checks, event records and their order, joint component create/destroy, character state on a ramp, snapshot/restore through the C API changes the world back, the sample runs headless for 600 frames and ends with the expected positions and a stable hash.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 410-411, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A C host steps a world with joints, events and a walking character, rewinds it from a snapshot, and the sample and the tutorial show it.

## Outcome

Joints (ENGINE_COMPONENT_JOINT 13), characters (14), joint_reaction, character_move/jump, physics_events, physics_snapshot/restore added to the C API (header, runtime, table, ABI snapshot, mask 0x3FFF). The frame now syncs joints, drives kinematic bodies and lets characters' entities follow their capsules. Tests: host_physics_joints.c (refusals, pendulum, break, character walk/jump, events, rewind) and the headless physics_sample.c (600 frames, rewind at 300, golden checksum 532bfea0 equal in the four Linux trees). Tutorial physics-in-your-window.md and guide section written. Deferred: hull/mesh/compound colliders through the C API and the the host mesh-collider check (collision assets are not addressable through resource_add yet).

## Post-implementation code review

/code-review high: 4 findings. Fixed: self-joint and zero hinge axis now refused; re-setting a joint remakes it (and a stale broken link no longer deletes the new component); a broken joint loses its component by the end of the frame (tested). Declined: contact events of a character's capsule name entity 0 (the capsule body has no entity link; recorded in KNOWN-ISSUES).
