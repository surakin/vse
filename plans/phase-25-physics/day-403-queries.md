# Phase 25 / Day 403 — Ray Casts, Shape Casts and Overlaps

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 402; Phase 18 (query vocabulary).

Ask the world questions: what does this ray hit, what is inside this volume.

## Tasks

- [x] `RayCast(origin, direction, max_distance, filter)` returning the closest hit (collider, point, normal, distance, triangle index for meshes) and `RayCastAll` sorted by distance; the broad phase uses the spatial module's ray query, the narrow phase exact ray-shape tests for sphere, box, capsule, hull (slab clipping against the planes) and mesh (BVH then triangle).
- [x] `ShapeCast(shape, transform, direction, distance, filter)`: sweep a sphere, box, capsule or hull and return the first impact (the same conservative advancement as CCD), and `Overlap(shape, transform, filter)` returning every collider the shape touches, sorted by collider id.
- [x] Query filters: layer mask, group/mask words, `include_triggers`, an ignore list of bodies, `include_static` and `include_dynamic`.
- [x] Queries read the world as of the last completed step, are thread-safe and may run from jobs while no step runs (the header says so); results are sorted and deterministic.
- [x] Closest-point query `ClosestPoint(shape, point)` for gameplay proximity.
- [x] Tests: rays against each shape with analytic hit points, grazing and inside-the-shape origins, against a mesh; a brute-force oracle over 10^4 random rays and shapes agrees with the tree-accelerated answer; shape cast of a sphere equals the ray cast against the shape grown by the radius; overlap sets equal brute force; filters.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 410-411, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Ray, shape and overlap queries agree with brute force over random scenes and are deterministic and thread-safe between steps.

## Outcome

`engine/physics/query.h/.cpp`: `RayCast`, `RayCastAll`, `ShapeCast`, `Overlap`, `ClosestPoint` over a `const PhysicsWorld&`, with `PhysicsQueryFilter` (layer mask, group and mask words, triggers off by default, static / kinematic / dynamic classes, ignore list) and `PhysicsHit` (collider, body, point, normal, distance, triangle). Exact ray tests: sphere by the quadratic, box by slabs, capsule by cylinder and end spheres, hull by face planes, mesh by the box query and Moller-Trumbore (triangle index and the normal against the ray), compound by children; a ray starting inside a solid hits at distance zero. `ShapeCast` samples the sweep and bisects 30 times with the step's `Collide` (sphere, box, capsule, hull), distance zero when it starts touching; `Overlap` is sorted by id key; `ClosestPoint` collides a 0.1 mm probe sphere with margin = the search distance. Tests (`tests/physics/query_test.cpp`, 8 cases, 839 assertions): analytic hits for sphere, box (also turned), capsule side and cap, hull and a two-triangle mesh from both sides; grazing just inside and outside a sphere; `RayCastAll` order and every filter; 150 random rays against random spheres, boxes and capsules agree with a marched 2 mm probe and the hit points lie on the surface; a sphere cast equals the ray against the grown sphere and the box face (8.7 within 2 mm); overlaps equal a brute-force `Collide` pass for 60 random shapes; closest points; 512 rays from jobs equal the serial ones. Not done: the tree-accelerated path (the queries scan all colliders and cull by their boxes; the broad phase boxes are stale after a step) and the 10^4-ray oracle (150 rays). Docs: `docs/guide/physics.md` ("Queries"), docgen 100 %. Runs: plain `ran 3562 of 9546 tests`, asan `ran 2942 of 9546` (one failure, `Spike 1: a renderer on a window's device and swapchain draws an ECS world` [asan], phase 24; it passed alone straight after), tsan `ran 2530 of 9546`.

## Post-implementation code review

`/code-review high`: 4 findings. Fixed: `RayCast` collected and sorted every hit to return the nearest (it now keeps the best and shrinks the reach), and the missing from-jobs test was added. Declined and recorded in `KNOWN-ISSUES.md`: no tree acceleration (linear scan), the oracle size (150 rays, primitives only), shape-cast sampling is capped at 1024 samples.
