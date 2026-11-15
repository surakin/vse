# Phase 27 / Day 362 — Large 2D Glyph Meshes: Cache & Edge Ramp

**Phase:** [Phase 27 — 3D Text, Large 2D Text & Colored Glyphs](../phase-27-3d-text-and-color-glyphs.md)
**Depends on:** Day 361; the glyph atlas keys (Phase 14 Day 177).

The cached per-glyph geometry the 2D mesh path draws, with the antialiasing fringe, still CPU-only and tested with a software rasterizer.

## Tasks

- [ ] **`text::GlyphMeshCache`:** key (font id, glyph id, tolerance bucket); value `GlyphMesh2D { vertices (position in em units, outward normal, alpha), indices, bounds, advance }` built as flatten -> cleanup -> triangulate -> fringe; thread-compatible; byte budget with least-recently-used eviction (the atlas has none; this cache must, the spec's number), counters (`hits`, `misses`, `evictions`, `bytes`, `incomplete`); the cache survives a device reset because it is CPU data.
- [ ] **Fringe construction:** along every boundary loop a strip of quads: inner vertices (alpha 1, normal 0) on the outline, outer vertices (alpha 0) on the outline with the averaged unit outward normal and a miter-limited offset direction; sharp corners get a bevel fan so the fringe never inverts; holes' fringes go into the hole.
- [ ] **Instance building:** `AppendGlyphInstance(const GlyphMesh2D&, pixel_size, origin_px, Vector<Vertex2D>&, Vector<u32>&)` scales em units to pixels, translates to the layout's pen position (the pen positions come unchanged from `TextLayout`, so nothing moves relative to the atlas path), and snaps nothing (large text is not pixel-snapped).
- [ ] **Software reference rasterizer** in `tests/text/soft_raster.h` (triangles with per-vertex alpha, the fringe expanded by `fringe_px` as the shader will): the quality oracle for Days 362-363.
- [ ] **Tests** (`tests/text/glyph_mesh_test.cpp`): cache hit/miss/eviction and byte accounting; two sizes in one bucket share a mesh; a hand-made square: raster coverage at 200 px equals the exact area within 1 percent and edge pixels are partial (a straight horizontal edge at a half-pixel offset gives about 50 percent coverage); every ASCII glyph at 160, 256 and 512 px compared with `Font::Render` at the same size where possible (the maximum is 128, so compare the 128 px render with the mesh rasterized at 128 px: intersection over union at least 0.97); the fringe never overlaps the interior of another part of the glyph for a set of thin glyphs (`i`, `l`, `-`); the instance's pen position equals `TextLayout`'s.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (the cache is shared state: run the tsan flavor of its test once with two threads reading); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC no later than Day 373; until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/text.md` "Large text" (draft, finished Day 363).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** large-glyph geometry is cached with eviction, antialiased by a size-independent fringe, and matches FreeType's raster of the same glyph closely, all checked without a GPU.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
