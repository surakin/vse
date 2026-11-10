# Phase 25 / Day 253 — Triangulating Polygons with Holes

**Phase:** [Phase 25 — 3D Text, Large 2D Text & Colored Glyphs](../phase-25-3d-text-and-color-glyphs.md)
**Depends on:** Day 252.

Own ear-clipping triangulator with hole bridging; the shared core of the large 2D text and the 3D faces. No third-party tessellator.

## Tasks

- [ ] **`Triangulate(const CleanShape&) -> TriangleMesh2D { Vector<Point2> vertices; Vector<u32> indices; }`:** per outer, bridge each hole to the outer along the shortest visible bridge (rightmost-hole-vertex rule), ear clipping on the merged simple polygon, collinear and duplicate points tolerated, counter-clockwise triangles; vertices are the loop points only (no new vertices except the duplicated bridge endpoints).
- [ ] **Failure policy:** if the polygon cannot be fully triangulated (numerical trouble), return the triangles found, set `Result::incomplete`, count it and log once per glyph; the renderer then draws what exists (never a crash, never an infinite loop).
- [ ] **Indices:** 16-bit when the vertex count fits, else 32-bit, chosen by the caller of the mesh builder (`MeshData` rule); also an optional strip conversion using the Phase 5 `ListToStrip` helper where it shrinks the index count (measured; used by 3D text in Day 258 only if smaller).
- [ ] **Tests** (`tests/text/triangulate_test.cpp`): triangle, convex and concave polygons, a polygon with one and several holes, a hole touching a vertex, collinear runs, a thin sliver, 1000 random star polygons (fixed seed): in all of them the triangle areas sum to the polygon area minus the holes (relative error 1e-6), every triangle is counter-clockwise and non-degenerate, no triangle overlaps another (area union by pixel-grid count), `V - 2` triangles for a simple polygon without holes and `V - 2 + 2h` with h bridged holes; every glyph of Liberation Sans at the three tolerance buckets passes the same area check against Day 252's `CleanShape`; the `incomplete` path with a deliberately self-touching input.
- [ ] **Benchmark:** time and triangle count per glyph (ASCII set) at the three buckets, recorded here, with the cache budget it implies (the spec's number from Day 250).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (pure CPU); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC no later than Day 265; until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/text.md` "Outline geometry": triangulation.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** every glyph of the shipped font triangulates to a set of triangles whose area matches the outline and whose winding is consistent, failures degrade without crashing.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
