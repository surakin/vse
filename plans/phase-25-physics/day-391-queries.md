# Phase 25 / Day 391 — Ray Casts, Shape Casts and Overlaps

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 390; Phase 18 (query vocabulary).

Ask the world questions: what does this ray hit, what is inside this volume.

## Tasks

- [ ] `RayCast(origin, direction, max_distance, filter)` returning the closest hit (collider, point, normal, distance, triangle index for meshes) and `RayCastAll` sorted by distance; the broad phase uses the spatial module's ray query, the narrow phase exact ray-shape tests for sphere, box, capsule, hull (slab clipping against the planes) and mesh (BVH then triangle).
- [ ] `ShapeCast(shape, transform, direction, distance, filter)`: sweep a sphere, box, capsule or hull and return the first impact (the same conservative advancement as CCD), and `Overlap(shape, transform, filter)` returning every collider the shape touches, sorted by collider id.
- [ ] Query filters: layer mask, group/mask words, `include_triggers`, an ignore list of bodies, `include_static` and `include_dynamic`.
- [ ] Queries read the world as of the last completed step, are thread-safe and may run from jobs while no step runs (the header says so); results are sorted and deterministic.
- [ ] Closest-point query `ClosestPoint(shape, point)` for gameplay proximity.
- [ ] Tests: rays against each shape with analytic hit points, grazing and inside-the-shape origins, against a mesh; a brute-force oracle over 10^4 random rays and shapes agrees with the tree-accelerated answer; shape cast of a sphere equals the ray cast against the shape grown by the radius; overlap sets equal brute force; filters.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Ray, shape and overlap queries agree with brute force over random scenes and are deterministic and thread-safe between steps.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
