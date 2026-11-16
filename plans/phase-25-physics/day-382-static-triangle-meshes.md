# Phase 25 / Day 382 — Static Triangle Mesh Colliders

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 381; Phase 18 (`StaticBvh`).

Level geometry: a static mesh that dynamic bodies collide with.

## Tasks

- [ ] Minted mesh-collider asset (`MCOL`): positions, triangle indices, per-triangle material index and a baked BVH, deterministic, with the validation of the importer's hostile-input rules (index range, finite positions, a triangle limit); a mesh made from a glTF mesh by `asset_mint` (reusing the importer's output blobs).
- [ ] Runtime `TriangleMesh` view (zero-copy over `SharedBytes`), the BVH reuses `spatial::StaticBvh` over triangle bounds; queries by box, by ray and by shape sweep return triangle indices in sorted order.
- [ ] Triangle-versus-shape contacts: sphere, capsule, box and hull against a triangle (the convex-versus-triangle case through GJK/EPA with the triangle as a degenerate hull), edge and vertex normals corrected so a body sliding over an edge between two coplanar triangles does not catch (the internal-edge rule: a contact normal that points into the neighbour's face is replaced by the face normal).
- [ ] One-sided triangles are not supported (a mesh is a closed or open solid by its triangles, both sides collide); the spec's limits (a body deeper than a triangle's thickness can fall through) are stated in the guide.
- [ ] Meshes are static only: attaching one to a dynamic body is refused at component validation.
- [ ] Tests: a sphere rolls down a ramp mesh and stops at the bottom; a box slides across two coplanar triangles with no bump (the internal-edge rule); a capsule against a mesh corner; a 100k-triangle mesh builds in a bounded time and the query count per step is logged; hostile mesh data refused (bad indices, NaN, huge triangle counts); the minted blob is deterministic.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Dynamic bodies roll and slide over a minted triangle mesh without catching on internal edges, and hostile mesh data is refused.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
