# Phase 27 / Day 337 — Flattening Glyph Outlines

**Phase:** [Phase 27 — 3D Text, Large 2D Text & Colored Glyphs](../phase-27-3d-text-and-color-glyphs.md)
**Depends on:** Day 336; `Font::LoadOutline` (Phase 14 Day 176).

Turn a glyph's quadratic/cubic contours into polylines at a chosen tolerance. Pure CPU, in `engine/text`, no GPU.

## Tasks

- [ ] **Types** (`engine/text/outline_geometry.h`): `Point2` (f32 em units), `Polyline { Vector<Point2> points; bool closed; }`, `PolygonSet { Vector<Polyline> contours; }`; scale from font units by `units_per_em` (read through the `Font`, added to `FontMetrics` if absent).
- [ ] **`FlattenOutline(const Outline&, f32 units_per_em, f32 tolerance_em) -> PolygonSet`:** reads on/off-curve points per TrueType rules (implied on-curve midpoints between two off-curve points; a contour that starts off-curve), cubic segments for CFF (`OutlinePoint::cubic`); recursive or fixed-step subdivision whose maximum deviation from the curve is at most `tolerance_em`; contours closed; consecutive duplicate points removed; zero-area contours (a single point, two points) dropped and counted.
- [ ] **Determinism and bounds:** the same input gives byte-identical output; the number of points per curve is capped (a documented maximum) so a tiny tolerance cannot allocate without bound; a NaN or infinite tolerance is refused (`Aborts:`).
- [ ] **Tests** (`tests/text/flatten_test.cpp`, phase 27): a hand-made quadratic and cubic against the analytic curve (max deviation <= tolerance, sampled at 1000 parameter values); implied on-curve points; a contour starting with an off-curve point; closed contours; smaller tolerance never gives fewer points; the cap; every glyph of Liberation Sans flattens with at least 3 points per contour at the three tolerance buckets and the polygon area differs from FreeType's rasterized coverage area (`Font::Render` at 128 px) by under 1 percent per glyph; a CFF-style cubic outline built by hand (Liberation is TrueType only).
- [ ] **Benchmark:** flatten time per glyph over the ASCII set, recorded in this file.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (pure CPU; only if a container or lifetime bug was fixed); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC no later than Day 351; until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/text.md` gets a section "Outline geometry" (flatten) with a tagged sample that compiles and runs.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** flattening is exact within the tolerance for quadratic and cubic outlines, deterministic, bounded, and tested against every glyph of the shipped font.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
