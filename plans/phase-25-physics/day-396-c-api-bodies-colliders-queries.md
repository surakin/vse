# Phase 25 / Day 396 — C API I: Bodies, Colliders, Forces and Queries

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 395; Phases 23-24 (`engine_c`).

A host puts physical entities in its world and moves them with forces.

## Tasks

- [ ] New components in `include/engine/engine_c.h`: `ENGINE_COMPONENT_RIGID_BODY` (`engine_rigid_body`: type, mass, damping, gravity scale, flags) and `ENGINE_COMPONENT_COLLIDER` (`engine_collider`: shape kind, sizes, offset, layer, group, mask, friction, restitution, trigger flag, a resource id for hull and mesh); size-prefixed structs, additions at the end of the enum and the table, the mask constants and `ENGINE_COMPONENT_MASK_ALL` updated (the Day 360 review found this exact bug: check `query` with the new bits).
- [ ] Table functions (end of `engine_api`): `physics_set_gravity`, `body_apply_force`, `body_apply_impulse`, `body_set_velocity`, `body_get_velocity`, `body_teleport`, `physics_raycast`, `physics_overlap_sphere` (and box, capsule), each validating handle and finite input, size-prefixed result structs, no pointer into engine memory crosses; results of queries come in an `engine_buffer` released by the host or into a host array with a capacity and a total (the pattern of `query`).
- [ ] `frame` runs the physics step in the documented order (after animation input, before transform and render sync), its cost appears in `engine_frame_stats` (added fields with the struct_size growth rule honoured: the Day 372 review's fix applies, the first-version sizes stay valid).
- [ ] Physics settings through the C API's engine settings (`physics.*`), with unknown names refused as for the other settings.
- [ ] ABI snapshot regenerated and `abi_check` updated; the C header still compiles as C; `ENGINE_ABI_VERSION` unchanged (unreleased) per the Day 358 spec, stated in the day file.
- [ ] Tests (a new `host_physics.c`, with its `std_usage_check` and CMake stanzas): argument checks for every function (null, stale, wrong type, non-finite, wrong struct size, an old-size struct), a ball dropped from the host falls and bounces (the entity's transform read back), a box pushed by an impulse, a ray cast hits the floor at the analytic point, an overlap lists the right entities, `query` on the new component bits, destroy order (world with bodies destroyed under a bound surface), an older-struct-size host.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A C host makes bodies and colliders, steps them through `frame`, pushes them and casts rays, with every new function argument-checked.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
