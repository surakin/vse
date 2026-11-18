# Phase 27 / Day 466 — Physics Bindings I: Bodies, Forces and Queries

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 450, 452, 456; Phase 25.

Scripts drive and question the physics world.

## Tasks

- [x] `RigidBody` and `Collider` through the component bindings (Day 456) with validation by the physics `Validate()` functions; `physics.add_force/impulse/torque(e, v)`, `set_velocity`, `get_velocity`, `set_gravity_scale`, `wake`, `sleep` as `world-write` commands applied at the sync point before the physics step (the order in the frame is fixed and documented).
- [x] Queries: `physics.raycast(origin, dir, max, mask)`, `shape_cast`, `overlap_sphere/box`, `closest_point` as `world-read` calls against the **tick-start physics state** (the world is not stepping during the script phase, so the read is safe from every partition at once; the physics query structures are read-only then, checked in the test); results as `Value`s with entity handles.
- [x] Layers and masks by name (a settings-defined table) in the bindings, not raw bits.
- [x] Kinematic bodies moved by scripts through the Phase 25 drive path; joints created and removed through the joint components.
- [x] Frame order documented and tested: script phase, merge, physics `SyncBodies`, physics step, write-back, transform propagation, render sync; a script sees last step's results.
- [x] Tests: forces applied from 64 partitions at once sum correctly in the merge order (bit-identical at 1 and 16 threads, using the physics determinism hash), queries from many threads under tsan, validation errors.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

## Notes

- `script_physics` is a new target (layer 14, plain/asan/tsan) above `script` and `physics`; `script` stays free of `physics` through opaque hooks: service slots (`MergeServices::extensions`, copied to `PartitionContext::extensions`), `ScriptRuntime::Kinds()` and `ComponentCatalog::SetValidator` (the validator runs at `component.set/add`, so `{mass = -1}` fails at the call with the physics reason).
- Command kinds 100-109 (force, impulse, torque, set_linear, set_angular, gravity_scale, wake, sleep, add_joint, remove_joint); forces and impulses add up in the merge order; velocity writes and joints count as writes for the conflict policy.
- New engine call: `PhysicsWorld::Sleep(BodyId)` (the island goes to sleep; test in `tests/physics/sleep_test.cpp`).
- Layers by name: setting `script.physics_layers` (`PhysicsLayers`, restart required).
- Frame order is in `physics_bindings.h` and the guide, and tested (`script_physics_test.cpp`): script phase, merge, `SyncBodies`/`SyncJoints`, `DriveKinematicBodies`, step, write-back; a script sees the last step's results.
- Fixed on the way: `script_runtime.cpp` failed in a GCC unity build (`-Wsubobject-linkage`, seen at HEAD): the runtime's types are in `runtime_detail`, a named namespace.
- Pages touched: `docs/guide/scripting.md` ("Physics"), `docs/reference/script_physics.md` (new), `script.md`, `physics.md`, `script-api.md`, `symbols.txt`.

## Runs

- Phase 27 plain: `ran 244 of 11132 tests (phases: 27; flavors: plain; display: excluded; crash tests: excluded) in 303 s`
- Phase 27 asan: `ran 237 of 11132 tests (phases: 27; flavors: asan; display: excluded; crash tests: excluded) in 19 s`
- Phase 27 tsan: `ran 237 of 11132 tests (phases: 27; flavors: tsan; display: excluded; crash tests: excluded) in 20 s`
- Phase 25 (the module `Sleep` touches) plain: `ran 230 of 11132 tests (phases: 25; flavors: plain; ...) in 208 s`; asan: `ran 226 of 11132 tests (phases: 25; flavors: asan; ...) in 14 s`.
- Reduced runs, not the whole suite. After the review's format pass: `script_physics_tests` 5612 assertions in 20 test cases pass; gates 51/51 (module_deps, phase labels, std_usage, shared_unit, platform_neutral, module map paths); `ctest -L docs` 9/9; docgen lint at zero.
- Compiles clean with Clang plain (unity), GCC and Clang non-unity; GCC unity compiles the day's targets (`engine/net` fails there, known). Windows and macOS not run (Days 473-474).

## Post-implementation code review

`/code-review high`: 5 findings.
1. `set_gravity_scale` not visible to a same-tick `component.get`: declined, by design (the write changes the component at the merge; documented in the guide, `KNOWN-ISSUES.md`).
2. The joint payload is the raw `physics::Joint` bytes (padding): declined; the merge decodes fields only and nothing hashes payload bytes.
3. `Nested<>` field adapter belongs in `storage`: declined for now, it is used by one module; moved when a second needs it.
4. Lines over 120 columns in `PhysicsAddJoint`: fixed with `scripts/format.sh`.
5. Console and task states have no service slots: no change needed, they have no world context at all (recorded).
Earlier self-review fixes (no static layer table, deduped overlap results, `what` parameters) are in the commit.

**Done:** Scripts apply forces, move bodies and cast rays against a physics world, deterministically on any thread count.
