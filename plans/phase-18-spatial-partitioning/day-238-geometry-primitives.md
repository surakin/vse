# Phase 18 / Day 238 — Geometry Primitives (Shared With the Renderer)

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Day 237; Phase 5 `renderer::Aabb`, `BoundingSphere`, `Frustum`.

The shape types every spatial structure and query shares, in a layer both the spatial module and the renderer can include.

## Tasks

- [x] **Move, do not copy:** `Aabb`, `BoundingSphere`, `Bounds` and `Frustum` (now in `engine/renderer/bounds.h` and `frustum.h`) move to `engine/foundation/math/geometry.h` (namespace `vse`), with the renderer keeping `using` aliases so no caller changes; the frustum's matrix convention (depth 0..1, Vulkan) stays documented where it is built. `module_deps_check` and `docs/reference/modules.txt` follow.
- [x] **New types:** `Ray { Vec3 origin; Vec3 direction; }` with a precomputed inverse direction for slab tests, `Plane`, `Sphere`; helpers `Union`, `Intersection`, `Expand(margin)`, `Contains`, `Overlaps`, `SurfaceArea`, `Volume`, `Center`, `Transform(Aabb, Mat4)` (the exact box of a transformed box), `Distance2(point, Aabb)`.
- [x] **Tests for geometry:** box/box, box/sphere, sphere/sphere, box/frustum and sphere/frustum overlap against brute-force point sampling on 10,000 random shape pairs (fixed seeds); ray/box slab test against analytic cases (inside, grazing an edge, parallel to a face, behind the origin, direction with zero components), ray/sphere, ray/plane; `Transform(Aabb, Mat4)` equals the box of the eight transformed corners; every renderer geometry test still passes unchanged (`tests/renderer/geometry_test.cpp`).
- [x] **Module:** `engine/spatial` is created (empty CMake library with `_asan`/`_tsan` flavors, `tests/spatial/`, the `tests/phases.txt` line, the `modules.txt` row, `ARCHITECTURE.md` section 1 row); `docs/guide/spatial.md` is started with "Shapes".
- [~] **Runs (CLAUDE.md "Tests run by phase"; done on the Windows PC, so tsan, Clang and GCC wait for the Linux machine):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [~] **Two machines (CLAUDE.md "Windows is a supported platform"; the day was done on the Windows PC, so the open half is Linux):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "Shapes".
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the shared shape types live below both the renderer and the spatial module, the renderer builds and tests unchanged, and the new tests compare every overlap and ray test with a brute-force oracle.

## Notes

- **Done on the Windows PC** (2026-11-10, at the user's request), so the other machine is Linux: the Linux build (Clang and GCC, unity on and off) and the
  tsan flavor wait for it (`KNOWN-ISSUES.md`).
- **The move:** `engine/foundation/math/geometry.h` and `geometry.cpp` (in `foundation_base`) hold `Aabb`, `Sphere`, `Bounds`, `Plane`, `Containment`,
  `Frustum` and `ComputeAabb`/`ComputeBounds`/`TransformBounds`, with the code unchanged; `renderer/bounds.h` and `frustum.h` are now `using`
  declarations in `renderer::` (`renderer/bounds.cpp` and `frustum.cpp` deleted), so no caller changed and `tests/renderer/geometry_test.cpp` passes
  as it was. The frustum's convention (depth 0..1, `renderer::PerspectiveVulkan`) is documented on `Frustum::FromViewProjection`. The guide links to
  `[[renderer::Frustum]]` now name `[[Frustum]]`.
- **Names that differ from the task list:** the transformed box is `TransformAabb(box, m)`, not `Transform`, because `math/transform.h` has a type
  `Transform` in the same namespace (a function of that name would hide it); emptiness is the member `Aabb::IsEmpty()`, because `lang/traits.h` has a
  trait `IsEmpty<T>`. `Sphere` is the new type and the renderer's `BoundingSphere` is an alias of it (one type, not two). Also added: `Aabb::Empty()`
  (the identity of `Union`), `Aabb::FromMinMax`, `Aabb::Extents`, `Plane::FromPointNormal`, `Ray::Make` and `Ray::At`.
- **Rays:** a zero direction component takes its own branch in the slab test (inside that slab or never), so `0 * infinity` never makes a NaN; a ray
  starting inside a box or sphere hits at `t = 0`; grazes count (closed shapes).
- **The module:** `engine/spatial` is an interface library (`spatial`, `_asan`, `_tsan`; headers only: `spatial/spatial.h` with `kMaxSpatialSets` and
  `SpatialKey`) until Day 239 brings the first sources; `tests/spatial/` (phase 18, a new `flavors 18` line for Linux and Windows in `tests/phases.txt`),
  the `modules.txt` rows of `module_deps_check` and of the reference, `ARCHITECTURE.md` (layer 10 row, the dependency list), `docs/guide/spatial.md`
  ("Shapes", a runnable sample) and its line in the guide index.
- **Tests** (`tests/spatial/geometry_test.cpp`, 10 cases, 1.66 million assertions): box/box, box/sphere and sphere/sphere on 10,000 random pairs each
  (a sampled common point requires an overlap, a reported overlap must come with a witness point in both), box/frustum and sphere/frustum on 10,000
  shapes (a sampled point inside requires an overlap, and "culled" must equal "entirely beyond one plane"), the ray/box analytic cases (head on, any
  direction length, inside, along an edge, through a corner, parallel outside and inside a slab, zero components, behind, `max_t`, empty box), 2,000
  random rays against marching along them, ray/sphere and ray/plane, 1,000 random transforms against the eight corners, and the helpers.
- **Runs:** `scripts/test-phase.sh --affected` (everything: a `CMakeLists.txt` changed): `ran 1934 of 3773 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 252 s`, one failure,
  `test_phase_labels` (no `flavors` line for phase 18 yet: added, then it passed); asan `ran 1572 of 3773 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 123 s`, all passed.
  After the review fixes: `spatial_tests` and `spatial_tests_asan` 10 of 10, the renderer's bounds and frustum tests 4 of 4, the gates
  (`std_usage_check`, `module_deps_check`, `platform_neutral_check`, `raw_new_check`) pass, `ctest -L docs` 7 of 7 (91 samples, the new one included),
  `docgen lint` at 100 percent (`spatial` 2 of 2, `foundation/math` 153 of 153). Four Windows trees (MSVC plain and asan, clang-cl, unity on and off) clean.

## Post-implementation code review

`/code-review high` over the day's diff: 2 findings, both fixed, with one regression test that failed without the fixes (2 of its 5 checks):
`Geometry: an empty box stays empty through a transform and is never in a frustum`.

1. **Fixed:** `Overlaps(frustum, Aabb::Empty())` was true: the empty box's infinite corners give a NaN center, every plane comparison is then false and
   `TestAabb` answers `Inside`. `Overlaps(frustum, box)` now returns false for an empty box (`TestAabb` itself, the renderer's, is unchanged).
2. **Fixed:** `TransformAabb(Aabb::Empty(), m)` was a NaN box, which `IsEmpty()` does not count as empty, so a union would spread NaN. An empty box now
   stays `Aabb::Empty()`.
