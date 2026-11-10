# Phase 18 / Day 238 — Geometry Primitives (Shared With the Renderer)

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Day 237; Phase 5 `renderer::Aabb`, `BoundingSphere`, `Frustum`.

The shape types every spatial structure and query shares, in a layer both the spatial module and the renderer can include.

## Tasks

- [ ] **Move, do not copy:** `Aabb`, `BoundingSphere`, `Bounds` and `Frustum` (now in `engine/renderer/bounds.h` and `frustum.h`) move to `engine/foundation/math/geometry.h` (namespace `engine_placeholder`), with the renderer keeping `using` aliases so no caller changes; the frustum's matrix convention (depth 0..1, Vulkan) stays documented where it is built. `module_deps_check` and `docs/reference/modules.txt` follow.
- [ ] **New types:** `Ray { Vec3 origin; Vec3 direction; }` with a precomputed inverse direction for slab tests, `Plane`, `Sphere`; helpers `Union`, `Intersection`, `Expand(margin)`, `Contains`, `Overlaps`, `SurfaceArea`, `Volume`, `Center`, `Transform(Aabb, Mat4)` (the exact box of a transformed box), `Distance2(point, Aabb)`.
- [ ] **Tests for geometry:** box/box, box/sphere, sphere/sphere, box/frustum and sphere/frustum overlap against brute-force point sampling on 10,000 random shape pairs (fixed seeds); ray/box slab test against analytic cases (inside, grazing an edge, parallel to a face, behind the origin, direction with zero components), ray/sphere, ray/plane; `Transform(Aabb, Mat4)` equals the box of the eight transformed corners; every renderer geometry test still passes unchanged (`tests/renderer/geometry_test.cpp`).
- [ ] **Module:** `engine/spatial` is created (empty CMake library with `_asan`/`_tsan` flavors, `tests/spatial/`, the `tests/phases.txt` line, the `modules.txt` row, `ARCHITECTURE.md` section 1 row); `docs/guide/spatial.md` is started with "Shapes".
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "Shapes".
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the shared shape types live below both the renderer and the spatial module, the renderer builds and tests unchanged, and the new tests compare every overlap and ray test with a brute-force oracle.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
