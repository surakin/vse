# Phase 26 / Day 315 — Contour Orientation, Nesting & Overlap Removal

**Phase:** [Phase 26 — 3D Text, Large 2D Text & Colored Glyphs](../phase-26-3d-text-and-color-glyphs.md)
**Depends on:** Day 314.

Make the flattened contours safe for triangulation and extrusion: known orientation, outer/hole classification, and no overlaps or self-intersections.

## Tasks

- [ ] **Signed area and orientation** per contour (exact arithmetic on the f32 points widened to f64; deterministic summation order); contours with area below an epsilon dropped and counted.
- [ ] **Self-intersection and overlap removal (nonzero fill rule):** split every contour at all edge intersections (sweep or grid-accelerated segment intersection; robust predicates for orientation tests, with the epsilon policy written in the header), compute the winding number on each side of every split edge, keep the edges that separate winding zero from nonzero, re-link them into closed simple loops, and orient outer loops counter-clockwise and holes clockwise. Output: `CleanShape { Vector<Loop> outers; Vector<Loop> holes; }` plus for each hole the index of its containing outer.
- [ ] **Even-odd option** (`FillRule::EvenOdd`) for fonts that use it; default nonzero per the spec.
- [ ] **Degenerate input policy:** touching contours (a hole touching its outer at a point), coincident edges, collinear overlaps and zero-length edges each have a documented result; never an infinite loop (an iteration cap that fails with a counted `Status`, not a hang).
- [ ] **Tests** (`tests/text/contour_cleanup_test.cpp`): a square with a square hole (one outer, one hole); two overlapping squares (one outer whose area is the union, checked analytically); a figure-eight contour (self-intersection); a hole outside every outer becomes an outer; nested islands (outer in hole in outer); reversed input orientation gives the same output; coincident edges; the cap path; a randomized test (fixed seed) of random rectangles whose union area is checked against a pixel-grid count; every glyph of Liberation Sans yields simple loops (no pair of edges of the output properly intersects, checked by brute force) and the filled area equals the Day 314 area for glyphs without overlaps.
- [ ] **Overlap survey:** the list of glyphs of the shipped font that actually needed overlap removal is recorded in this file (from Day 313 experiment (c)).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (pure CPU); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC no later than Day 328; until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/text.md` "Outline geometry": cleanup and the fill rule.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** every glyph of the shipped font cleans to simple, correctly oriented, correctly nested loops, degenerate inputs have defined results, and nothing can hang.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
