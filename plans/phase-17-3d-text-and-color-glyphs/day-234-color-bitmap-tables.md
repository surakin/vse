# Phase 17 / Day 234 — `CBDT`/`CBLC` and `sbix` Bitmap Strikes

**Phase:** [Phase 17 — 3D Text, Large 2D Text & Colored Glyphs](../phase-17-3d-text-and-color-glyphs.md)
**Depends on:** Days 231 and 233.

Our own readers for embedded color bitmaps, since FreeType is built without PNG.

## Tasks

- [ ] **Table access:** raw tables through FreeType's `FT_Load_Sfnt_Table` behind `font_freetype.cpp` (the only file that includes FreeType), exposed as `Font::LoadTable(tag) -> Vector<byte>` (read-only copy, `Status` failure when absent).
- [ ] **`text::ColorBitmapReader`:** `CBLC` strike selection (the strike whose ppem is nearest to the request, ties to larger), index subtable formats 1-3 and 4-5 as the fonts use them, `CBDT` image formats 17 (small metrics + PNG), 18 (big metrics + PNG) and 19 (metrics in `CBLC` + PNG); `sbix` strikes by ppem with graphic type `png ` (and `dupe` redirection, depth-limited), origin offsets; every offset and length bounds-checked against the table size.
- [ ] **`Font::RenderColorBitmap(GlyphId, u32 pixel_size, ColorBitmap* out)`:** RGBA8 premultiplied, with left/top bearing and advance scaled from the strike's ppem to the requested size (the scale factor is returned; the atlas stores the strike-size bitmap and the quad is scaled, bilinear), a glyph without a strike returns `not_found`, a glyph whose PNG fails to decode returns `decode_failed` and falls back to the monochrome outline, counted in `FontColorStats`.
- [ ] **Unsupported:** raw (non-PNG) `CBDT` formats 1, 2, 5-9 and bit-packed formats are refused with a logged `unsupported` once per font; the test list names them.
- [ ] **Test fonts:** the builder of Day 231 gains `CBLC`/`CBDT` (formats 17 and 18, two strikes) and `sbix` (two strikes, one `dupe`), with PNGs from `EncodePngStored`.
- [ ] **Tests** (`tests/text/color_bitmap_test.cpp`): strike selection (request 20 with strikes 16 and 32 picks 16 or 32 by the documented rule; exact match; below and above all strikes), the three CBDT formats and `sbix` return the pixels written into the builder's PNG; metrics and bearings are scaled by the ratio; a `dupe` loop is cut at the depth limit; a refused format falls back with a count; offsets pointing outside the table (a sweep over every 4-byte field of a valid `CBLC` set to 0xFFFFFFFF and to 0) never crash; a glyph in the cmap but absent from the strikes keeps its outline.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors asan on the new tests; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC no later than Day 237; until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/text.md` "Colored glyphs": bitmap formats and what is not read.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** embedded PNG color bitmaps in `CBDT` and `sbix` fonts load through our own readers with strike scaling, fallbacks are counted and logged, and malformed tables cannot crash.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
