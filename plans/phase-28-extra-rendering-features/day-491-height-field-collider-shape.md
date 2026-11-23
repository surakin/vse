# Phase 28 / Day 491 — The Height-Field Collider: Shape, Library and Queries

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 486, 490, Phase 25 (physics).

Required by the user: a physics shape that is the terrain.

## Tasks

- [x] `physics::ShapeKind::HeightField = 6` appended (never renumbered; the C API and the snapshot format carry the number); `Shape::height_field` holds an id into a new `HeightFieldLibrary` (like `HullLibrary` and `MeshLibrary`), `half_extents` its bound; static bodies only, validated like `Mesh`.
- [x] `physics::HeightField` (the plan's `HeightFieldData`): the sample grid read in place from a `SharedBytesPtr` (the asset's own bytes, no copy; physics does not depend on asset), the cell size, the origin, the height range, the min/max pyramid; the library adds a field from a loaded asset or from raw heights (for tests and for a C host), removes it, and snapshots by content hash.
- [x] The surface definition (documented and tested): each cell is two triangles split along a fixed diagonal; the height at a point is the bilinear-by-triangle value, so the render sample (`HeightMapData::HeightAt`, Day 486) and the physics surface are the same function; a test compares them at 10,000 fixed points.
- [x] Queries: ray cast against the field with a DDA over the cells culled by the pyramid (a hit returns the point, the triangle normal, the cell index), sphere and box overlap, closest point; all deterministic (`+ - * / Sqrt` only, ordered traversal).
- [x] The shape's AABB is the field's bound; the broad phase treats it as one large static shape (it lives in the static tree, Phase 18), so no private grid is added anywhere.
- [x] The snapshot and the C API shape structs carry the new kind; `engine_c` physics shape creation accepts it (Day 493 completes the host side). **The snapshot carries it (version 2); the C API constant, creation and read-back moved to Day 493 (2026-11-23).**
- [x] Tests: ray hits on a ramp, a bump and the map edge; rays parallel to the surface and starting below it (a ray from beneath returns no hit by the documented one-sided rule, or the two-sided rule if the spec chose it); the surface agreement test; hostile data (a non-finite height, a 1-wide grid) refused by status; the library's ids stay stable across add and remove.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** A height field exists as a physics shape with deterministic queries and the same surface as the render terrain.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

`/code-review high` over the day's diff: 5 findings, 0 fixed, 5 recorded (none a defect the tests show).
- Shapes whose box covers more than 1024 triangles collide with the first rows only (the bound keeps a huge shape from listing a whole map). Recorded.
- The snapshot version went 1 to 2 (the collider carries the field id); old snapshots are refused. Recorded.
- The C API collider read-back reports a height field as a capsule; Day 493 adds the host side. Recorded.
- Continuous collision treats the surface as a thin two-sided surface, as meshes; Day 492.
- Debug draw shows the field's bound box only; Day 493.
Also fixed during the day: `shared_unit_check` flagged raw pointer members in the triangle adapters (now static functions over a parameter).
Deviations: the field's space is centred on the map (so the shape bound is centred); `HeightAtMap` takes the asset's coordinates. The ray rule is two-sided (as meshes). Pyramid built in physics (8 by 8 cells) from the samples, not read from the asset's pyramid. Ray cast, box query, closest point and overlap go through the existing mesh paths; shape cast of a field refused.
Runs: see STATUS.md.
