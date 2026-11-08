# Phase 14 / Day 177 — Glyph Rasterization and the Atlas Packer (CPU)

**Phase:** [Phase 14 — Font & Text Rendering](../phase-14-font-text-rendering.md)
**Depends on:** Day 176.

Turn glyphs into coverage bitmaps and pack them into atlas pages, entirely on the CPU, so the GPU side only has to upload pages.

## Tasks

- [ ] **Rasterization:** `Font::Render(GlyphId) -> GlyphBitmap` (R8 coverage, row pitch, left/top bearing, the hinting mode the spec chose); empty glyphs (space) give a zero-size bitmap with the right
      advance; the bitmap owns its memory through our containers (`Vector<byte>`).
- [ ] **Shelf packer** `text::ShelfPacker(width, height, padding)`: `Place(w, h) -> optional Rect`; 1 px padding (so bilinear sampling does not bleed), shelves by height class, rejects a glyph larger than the
      page, reports the used area; no allocation after construction except the shelf list; deterministic.
- [ ] **Atlas** `text::GlyphAtlas`: pages of R8 pixels held in CPU memory (the reset source of truth), `GetOrAdd(font, size, GlyphId) -> GlyphEntry {page, u0, v0, u1, v1, width, height, bearing, advance}`; a
      full page adds a page (up to a documented maximum, then `GlyphEntry` invalid with a counted failure — never a crash); dirty rectangles per page tracked so the GPU upload copies only what changed.
- [ ] **Maximum size:** `kMaxAtlasPixelSize` (value from Day 175) is a constant of the module; `Font::Render` and `GlyphAtlas::GetOrAdd` for a larger size return a `too_large` status and add nothing
      (counter `too_large`); a test at the maximum (accepted) and at maximum + 1 (refused), and that a refused call leaves the atlas byte-identical.
- [ ] **Keys and sharing:** one atlas serves several fonts and sizes; the key is (font id, pixel size, glyph id); two layouts of the same string add no second copy; counters (`glyphs`, `pages`, `hits`, `misses`,
      `bytes`) for tests and the benchmark.
- [ ] **Hinting decision recorded:** the mode chosen on Day 175 is the default; `FontOptions::hinting` exposes the others; a test renders "Hello" in each mode and checks coverage is within the expected
      range (not a golden picture: a property test that the sum of coverage is close to the glyph's area).
- [ ] **Tests** (`tests/text/glyph_atlas_test.cpp`, phase 14): the packer never overlaps two rectangles (randomized, 10k rectangles, fixed seed), respects padding and bounds, fills a page to a known
      fraction; the atlas returns the same entry for the same key, adds a page when full, counts failures, tracks dirty rectangles exactly (a placed glyph dirties exactly its rectangle); the 94 printable
      ASCII glyphs at 16 px pack into one 256x256 page; rasterized "Hello" bitmaps match PGM goldens committed by this day under `tests/text/golden/` (written from the new `Font::Render` and checked by eye against `docs/superpowers/specs/2026-11-08-text-hinting-samples.png`; the FreeType version is pinned, so they are stable).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code;
      quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with
      Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the day closes; a day done on one machine files
      "open until the other machine runs it" in `KNOWN-ISSUES.md` and the closing day clears it.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under
      "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** glyphs rasterize to coverage bitmaps and pack into CPU atlas pages with exact dirty tracking, a full page grows the atlas, and the packer and atlas tests pass under asan.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
