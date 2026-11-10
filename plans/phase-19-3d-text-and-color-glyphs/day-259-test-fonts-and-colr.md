# Phase 19 / Day 259 — Test Font Builder & COLR v0 Layers

**Phase:** [Phase 19 — 3D Text, Large 2D Text & Colored Glyphs](../phase-19-3d-text-and-color-glyphs.md)
**Depends on:** Days 251-258; FreeType color API (`FT_Palette_Select`, `FT_Get_Color_Glyph_Layer`).

Build the fonts the color tests need, and read layered color glyphs.

## Tasks

- [ ] **`tests/text/font_builder.h`:** writes a minimal valid TrueType font into a `Vector<byte>` from a description (glyph outlines: square, triangle, ring, a glyph with a hole; `head`, `hhea`, `maxp`, `hmtx`, `loca`, `glyf`, `cmap` format 4, `name`, `OS/2`, `post`), with optional `COLR`+`CPAL` v0 (layers and palettes, including the foreground color index 0xFFFF), a `COLR` v1 table (a base glyph list record) for the unsupported-format test; deterministic bytes; FreeType opens and renders every font it writes (a test), `Font::Open` accepts it.
- [ ] **`Font` API:** `bool HasColorLayers(GlyphId)`, `Status LoadColorLayers(GlyphId, u32 palette, Vector<ColorLayer>* out)` with `ColorLayer { GlyphId glyph; Color color; bool uses_foreground; }`, `u32 PaletteCount()`; palette selection by index; colors as the engine's `Color` (sRGB bytes with alpha, matching `ColorVertex`).
- [ ] **Unsupported formats fall back, logged once per font and format:** `COLR` version 1 (only the v0 records are read if present, else the monochrome outline), the `SVG ` table (ignored: monochrome outline), a glyph absent from the font (`.notdef`, already the Phase 14 behavior); `FontColorStats { color_glyphs, fallbacks_v1, fallbacks_svg }`.
- [ ] **Tests** (`tests/text/color_layers_test.cpp`): the builder's font round-trips (cmap, metrics, outline equal to the description); a glyph with three layers returns them in order with the palette colors; palette 1 differs from palette 0; the foreground index maps to `uses_foreground`; a v1 font logs once, returns the monochrome fallback and counts it; an `SVG ` font likewise; a truncated COLR table (every prefix length of a valid table, fixed seed for the cut points) never crashes and returns a `Status` failure or the fallback.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (FreeType allocation counting: run asan on the new tests); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC no later than Day 265; until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/text.md` "Colored glyphs" (formats, palettes, fallback).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** layered color glyphs load from a deterministic test font, unsupported color formats fall back with a counted, logged, tested behavior, and malformed tables cannot crash.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
