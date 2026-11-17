# Phase 28 / Day 444 — Design Spec, Decisions & Phase Setup

**Phase:** [Phase 28 — 3D Text, Large 2D Text & Colored Glyphs](../phase-28-3d-text-and-color-glyphs.md)
**Depends on:** Phase 14 complete (Days 175-183), Phase 16 closed on Linux.

Settle the questions PLAN left open, write the design spec, and set up the phase (`CURRENT_PHASE`, module rows, baselines) before any geometry code.

## Tasks

- [ ] **`CURRENT_PHASE` becomes 18** in the commit that completes this day's first task, together with the `tests/phases.txt` line for phase 28 and `flavors 18`.
- [ ] **Spec** `docs/superpowers/specs/2026-11-NN-outline-text-design.md` recording, with the experiment that justifies each, the decisions below (the planner's defaults are in brackets; an experiment that disproves one amends the day files that depend on it in the same commit):
      - 2D edge antialiasing [**coverage ramp**: every outline edge gets a one-pixel fringe whose outer vertices carry alpha 0 and a unit outward normal; the vertex shader moves them by `fringe_px` pixels, so one cached mesh serves every size; MSAA is not used because the overlay pass has no multisampled target];
      - where the mesh path takes over [above `kMaxAtlasPixelSize` (128 px); `TextRendererDesc::mesh_min_pixel_size` can lower it; Day 449 measures quality and cost at 96-160 px to confirm];
      - flattening tolerance [in em units (the glyph scaled so the em is 1): 2D uses `0.25 px / pixel_size` rounded down to a power-of-two bucket, so a mesh is cached per (font id, glyph id, bucket); 3D takes a tolerance in em units from the caller, default 1/256 em];
      - fill rule and contour handling [the font's rule is nonzero winding; contours are classified by computed orientation and nesting, never by assuming TrueType's clockwise/CFF's counter-clockwise; overlapping contours are merged by an arrangement pass (Day 446) so the triangulator and the extruder only see simple, non-overlapping polygons];
      - triangulation [ear clipping with hole bridging (Eberly), robust to collinear points; O(n^2) per glyph is fine because meshes are cached];
      - bevel [a straight chamfer or a rounded profile with `segments` steps, depth and inset in em units, miter limit; Day 451];
      - 3D text granularity [per-glyph meshes cached per (font, glyph, options); a string is the merge of its glyph meshes at their pen positions in one `MeshData` (one draw per string and layer); no per-character instancing];
      - the 2D mesh batch [glyph geometry copied into a per-frame vertex stream scaled and translated on the CPU, one draw per overlay batch];
      - colored glyphs: COLR v0 and CPAL through FreeType's color API, `CBDT`/`CBLC` and `sbix` bitmaps through FreeType with **vendored libpng and zlib** (the user's decision, 2026-11-10; Day 455).
- [ ] **Module placement:** `engine/text` gains the pure-CPU geometry (flatten, cleanup, triangulate, extrude, color layers), `engine/renderer` the GPU side; no new engine module (libpng and zlib are third-party libraries behind FreeType); `ARCHITECTURE.md` and `docs/reference/modules.txt` get the new headers; the third-party boundary rule for libpng/zlib (only FreeType includes them) is decided here.
- [ ] **Test fonts:** no emoji font is committed (licenses, size, and COLR v1/PNG formats we do not read). A deterministic builder `tests/text/font_builder.h` writes minimal TrueType files (square, triangle and ring glyphs, `cmap`, `COLR`/`CPAL` v0, a `COLR` v1 table for the fallback test, `CBLC`/`CBDT` and `sbix` with PNG strikes written with stored deflate blocks); Day 453 builds it. Record this in the spec.
- [ ] **Experiments:** (a) flatten and triangulate every glyph of Liberation Sans with a throwaway script and record vertex/triangle counts and time per glyph at the three tolerance buckets, to set the cache budget; (b) draw one large glyph as a filled mesh with and without the fringe into a PGM by a software rasterizer in the scratchpad, to confirm the ramp width; (c) list which glyphs of Liberation Sans have overlapping contours (composites, `Q`, `%`), to size Day 446.
- [ ] **Baselines** (Debug and release, Linux): `renderer_bench --section ui` and the text pass numbers from Phase 14/15 (`docs/guide/text.md`, "Cost"), recorded in the spec so Day 459 can show no regression for atlas text.
- [ ] **Plan check:** read Days 445-459 against the spec; amend any day the decisions changed; `PLAN.md` assumption #17 gets libpng and zlib (Day 455 writes the text) and a new assumption records the outline-mesh decisions; `KNOWN-ISSUES.md` gets "Phase 28 DXIL shaders wait for the Windows PC" as an expected item.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the spec records every decision with its evidence, `CURRENT_PHASE` is 17, the module rows and test-font plan are written, the baselines are recorded, and Days 445-459 are still accurate (or amended in this commit).

## Notes

- Planned 2026-11-10. The defaults in the brackets are the planner's; libpng and zlib are vendored at the user's request (2026-11-10).

## Post-implementation code review

(Filled in at the end of the day.)
