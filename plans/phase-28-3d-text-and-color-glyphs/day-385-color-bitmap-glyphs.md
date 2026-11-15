# Phase 28 / Day 385 — Color Bitmap Glyphs: `CBDT` and `sbix` through FreeType

**Phase:** [Phase 28 — 3D Text, Large 2D Text & Colored Glyphs](../phase-28-3d-text-and-color-glyphs.md)
**Depends on:** Days 382 and 369.

Load embedded color bitmaps with FreeType (`FT_LOAD_COLOR`, BGRA), pick the strike, scale to the request, and fall back in a counted way.

## Tasks

- [ ] **`Font::HasColorBitmap(GlyphId)` / `Font::RenderColorBitmap(GlyphId, u32 pixel_size, ColorBitmap* out)`** in `font_freetype.cpp` (still the only file that includes FreeType): `FT_Select_Size` on the strike nearest to the request (the documented rule: nearest ppem, ties to the larger), `FT_Load_Glyph` with `FT_LOAD_COLOR | FT_LOAD_RENDER`, the BGRA bitmap converted to RGBA8 premultiplied, bearings and advance scaled from the strike's ppem to the requested size (the scale factor is returned; the atlas holds the strike-size bitmap and the quad is scaled, bilinear); `FT_LOAD_NO_BITMAP` stays set on the existing monochrome path so ordinary text never picks a strike by accident.
- [ ] **Formats:** whatever FreeType reads: `CBDT`/`CBLC` PNG and raw formats, `sbix` `png ` strikes. A glyph without a strike returns `not_found`; a strike FreeType cannot decode returns `decode_failed`, falls back to the monochrome outline and is counted in `FontColorStats`; formats FreeType does not read (`COLR` v1, `SVG `) keep the Day 382 fallbacks.
- [ ] **Hostile strikes:** a strike whose declared size exceeds a documented limit (Day 384's experiment) is refused before decoding with `too_large`, counted.
- [ ] **Test fonts:** the builder of Day 382 gains `CBLC`/`CBDT` (formats 17 and 18, two strikes) and `sbix` (two strikes, one `dupe`), with PNGs from a small writer in the builder (`tests/text/png_writer.h`: stored deflate blocks, CRC-32 and Adler-32, test only).
- [ ] **Tests** (`tests/text/color_bitmap_test.cpp`): strike selection (request 20 with strikes 16 and 32; exact match; below and above all strikes), the CBDT formats and `sbix` return the pixels written into the builder's PNG; metrics and bearings scaled by the ratio; a `dupe` loop does not hang; a refused or corrupt PNG falls back and is counted; a sweep that sets each 4-byte field of a valid `CBLC`, and each byte of a valid PNG chunk header, to 0 and 0xFFFFFFFF never crashes (asan); an allocation-failure sweep (`FailAllocationsAfter`) over one bitmap load returns statuses and leaks nothing; a glyph in the cmap but absent from the strikes keeps its outline.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors asan on the new tests; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC no later than Day 388; until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/text.md` "Colored glyphs": bitmap formats and what is not read.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** embedded PNG color bitmaps in `CBDT` and `sbix` fonts load through FreeType with strike scaling, fallbacks and limits are counted and logged, and malformed tables and PNGs cannot crash.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
