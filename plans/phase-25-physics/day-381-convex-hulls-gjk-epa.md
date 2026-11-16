# Phase 25 / Day 381 — Convex Hulls, GJK and EPA

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 380; Phase 22 (`asset_mint`).

General convex shapes: the minted hull asset and the algorithm that collides anything convex.

## Tasks

- [ ] Minted hull asset (`HULL`): vertices, face planes and edges of a convex hull computed offline by `asset_mint` (quickhull, deterministic ordering, a vertex limit of 255, welding tolerance recorded), with the mass properties baked; a `check_mint.cmake` determinism test as for the other minters.
- [ ] Runtime `ConvexHull` view over the blob (zero-copy, `SharedBytesPtr`, offsets not pointers, per CLAUDE.md), `Support(direction)` by hill-climbing over adjacency with a start hint and by brute force for small hulls.
- [ ] GJK: distance and closest points between two convex shapes (Minkowski difference support), simplex handling with the robust sub-algorithms, termination by tolerance and iteration cap; works for sphere, box, capsule and hull through one `Support` interface.
- [ ] EPA for penetration: expands the polytope from the GJK terminal simplex, returns normal and depth; degenerate simplices (a flat tetrahedron) are repaired or fall back to the SAT of the primitives.
- [ ] Manifold from GJK/EPA: the one deepest point plus up to three more from face clipping of the hull's reference face (the box-box clipping of Day 380 generalised to polygons), so hull stacks hold.
- [ ] Collision dispatch updated: every pair involving a hull goes through GJK/EPA, the analytic paths stay for primitive pairs.
- [ ] Tests: GJK against the analytic primitive results (distance and closest points within 1e-4 on 10^5 random pairs), EPA depth against the SAT depth for boxes, a hull of a cube against the box shape, degenerate hulls refused at minting, hull-on-hull resting manifold has the expected points, the minted hull hash is stable and the hull's mass properties match the analytic cube.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Any convex hull collides with any other shape through GJK and EPA, agrees with the analytic results where both exist, and hulls are minted deterministically.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
