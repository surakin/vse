# Phase 27 / Day 466 — Physics Bindings I: Bodies, Forces and Queries

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 450, 452, 456; Phase 25.

Scripts drive and question the physics world.

## Tasks

- [ ] `RigidBody` and `Collider` through the component bindings (Day 456) with validation by the physics `Validate()` functions; `physics.add_force/impulse/torque(e, v)`, `set_velocity`, `get_velocity`, `set_gravity_scale`, `wake`, `sleep` as `world-write` commands applied at the sync point before the physics step (the order in the frame is fixed and documented).
- [ ] Queries: `physics.raycast(origin, dir, max, mask)`, `shape_cast`, `overlap_sphere/box`, `closest_point` as `world-read` calls against the **tick-start physics state** (the world is not stepping during the script phase, so the read is safe from every partition at once; the physics query structures are read-only then, checked in the test); results as `Value`s with entity handles.
- [ ] Layers and masks by name (a settings-defined table) in the bindings, not raw bits.
- [ ] Kinematic bodies moved by scripts through the Phase 25 drive path; joints created and removed through the joint components.
- [ ] Frame order documented and tested: script phase, merge, physics `SyncBodies`, physics step, write-back, transform propagation, render sync; a script sees last step's results.
- [ ] Tests: forces applied from 64 partitions at once sum correctly in the merge order (bit-identical at 1 and 16 threads, using the physics determinism hash), queries from many threads under tsan, validation errors.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Scripts apply forces, move bodies and cast rays against a physics world, deterministically on any thread count.
