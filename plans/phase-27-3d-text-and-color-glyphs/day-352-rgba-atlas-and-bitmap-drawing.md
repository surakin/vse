# Phase 27 / Day 352 — RGBA Atlas Pages & Bitmap Color Glyphs on 2D and 3D

**Phase:** [Phase 27 — 3D Text, Large 2D Text & Colored Glyphs](../phase-27-3d-text-and-color-glyphs.md)
**Depends on:** Days 349 and 351.

Draw bitmap color glyphs: an RGBA page type in the atlas for the 2D path, a textured quad for the 3D path.

## Tasks

- [ ] **Atlas:** `GlyphAtlas` pages get a format (`R8` coverage as now, `RGBA8` color); `GetOrAdd` for a color bitmap chooses the RGBA pages (their own packer and page budget, counters per format), key unchanged plus a `kind` bit; dirty rectangles and CPU copies work for both (the CPU copy is the reset source).
- [ ] **`GlyphAtlasTexture`** (renderer) uploads RGBA pages as `RGBA8` sRGB textures; `TextRenderer` gains a second atlas pipeline (premultiplied color sample times the draw color's alpha only; the draw color's RGB is ignored for bitmap glyphs, documented); batching by (page, format) preserves call order.
- [ ] **Large sizes:** a bitmap glyph above the mesh switchover size is drawn from its strike bitmap scaled (bilinear), since there is no outline to mesh -- documented as the one case where large text can be soft; counted in `TextStats` (`scaled_bitmap_glyphs`).
- [ ] **3D:** a bitmap color glyph in `BuildTextMesh` becomes a textured quad (or a slab of `depth_em` with the bitmap on the front face) sampling a per-string RGBA texture built from the glyph bitmaps (`Texture` owner with CPU source for reset); the material needs the base-color texture permutation (the existing check reports a mismatch).
- [ ] **Reset:** the RGBA page textures and the 3D bitmap texture register for `Release`/`Recreate`; a `reset_rig.h` test compares counts.
- [ ] **Tests:** CPU: RGBA page packing, counters, the CPU copy equals the uploaded data; GPU per backend: a builder-font bitmap glyph at 16 and 32 px draws its strike pixels at the expected positions (readback), the draw color's alpha scales it, a layered COLR glyph and a bitmap glyph in the same string keep call order, a bitmap at 300 px is drawn scaled and counted; 3D: the quad's pixels sample the bitmap colors; reset keeps counts; goldens for 2D and 3D bitmap rendering.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC no later than Day 354; until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/text.md` "Colored glyphs" finished; `docs/guide/renderer.md` atlas formats.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** bitmap color glyphs render in color on the 2D atlas path and in 3D, survive a device reset, and large sizes degrade by a documented, counted scale.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
