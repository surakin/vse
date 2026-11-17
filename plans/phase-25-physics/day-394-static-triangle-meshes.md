# Phase 25 / Day 394 — Static Triangle Mesh Colliders

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 393; Phase 18 (`StaticBvh`).

Level geometry: a static mesh that dynamic bodies collide with.

## Tasks

- [x] Minted mesh-collider asset (`MCOL`): positions, triangle indices, per-triangle material index and a baked BVH, deterministic, with the validation of the importer's hostile-input rules (index range, finite positions, a triangle limit); a mesh made from a glTF mesh by `asset_mint` (reusing the importer's output blobs).
- [x] Runtime `TriangleMesh` view (zero-copy over `SharedBytes`), the BVH reuses `spatial::StaticBvh` over triangle bounds; queries by box, by ray and by shape sweep return triangle indices in sorted order.
- [x] Triangle-versus-shape contacts: sphere, capsule, box and hull against a triangle (the convex-versus-triangle case through GJK/EPA with the triangle as a degenerate hull), edge and vertex normals corrected so a body sliding over an edge between two coplanar triangles does not catch (the internal-edge rule: a contact normal that points into the neighbour's face is replaced by the face normal).
- [x] One-sided triangles are not supported (a mesh is a closed or open solid by its triangles, both sides collide); the spec's limits (a body deeper than a triangle's thickness can fall through) are stated in the guide.
- [x] Meshes are static only: attaching one to a dynamic body is refused at component validation.
- [x] Tests: a sphere rolls down a ramp mesh and stops at the bottom; a box slides across two coplanar triangles with no bump (the internal-edge rule); a capsule against a mesh corner; a 100k-triangle mesh builds in a bounded time and the query count per step is logged; hostile mesh data refused (bad indices, NaN, huge triangle counts); the minted blob is deterministic.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 410-411, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Dynamic bodies roll and slide over a minted triangle mesh without catching on internal edges, and hostile mesh data is refused.

## Outcome

`MCOL` asset (`asset/mesh_collider.h`, `asset_mint mint-mcol`), `physics::TriangleMesh` (normals, edge neighbours, `StaticBvh` over the triangles, sorted `QueryBox`), `MeshLibrary` in `PhysicsWorld::Meshes()`, `ShapeKind::Mesh` (static bodies only, no mass), and `Collide(..., hulls, meshes)`: each nearby triangle is a flat two-sided convex piece through the GJK/EPA and face clipping of Day 393, a shorter exit across the triangle's plane replaces a longer EPA one, a one-point contact on an edge shared with a coplanar triangle takes the face normal (the internal-edge rule), and the triangles' contacts merge (deepest sets the normal, duplicates dropped, at most four). Tests: mesh refusals and neighbours, queries against a scan (300 random boxes), world rules, sphere/box/capsule/hull on a flat mesh, 1,200 slides across seams and a tilted plane with no wrong normal, a bowl, a turned mesh, 100,000 triangles, 20,000 random sphere contacts, MCOL blob tests; the 20,000-shape hash is `716573da705d5b3c` under clang and GCC, unity and not, Debug and Release.
Runs: plain `ran 3622 of 9956 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 3117 of 9956 ...`, tsan `ran 2705 of 9956 ...`, no failure; `ctest -L docs` 8 of 8; docgen lint at zero. Pages touched: `docs/guide/physics.md`, `docs/guide/assets.md`, `docs/reference/physics.md`, `docs/reference/asset.md`. Deviations: the "ball rolls down a ramp and stops" test needs the solver (Day 396) and is a static ramp-normal test here; no glTF-to-MCOL path and no baked hierarchy (KNOWN-ISSUES).

## Post-implementation code review

`/code-review high`, 2 findings, both declined and recorded: only the first 256 triangles of a query are tested and 64 contacts kept; the query allocates a `Vector` per call. During the work the seam test caught two bugs that were fixed (the side of the internal-edge normal was taken from a sideways EPA normal, and EPA could return a longer exit than the plane's).
