# Phase 14 / Day 176 — Font Faces: Loading, Metrics, Glyph Lookup, Kerning

**Phase:** [Phase 14 — Font & Text Rendering](../phase-14-font-text-rendering.md)
**Depends on:** Day 175.

A `text::Font` that loads a font file through the file layer and answers every question layout needs, with errors as values.

## Tasks

- [ ] **Loading:** `Font::Open(ByteSpan data, FontOptions)` (the caller reads the file through `io::File`; the font copies or keeps the bytes as the spec decided — FreeType needs the memory alive,
      so the `Font` owns them) and `OpenFile(io::Path)` convenience; face index; errors as `text::Status` (not found, not a font, unsupported format, out of memory) with a message that names the cause.
- [ ] **Sizing:** `SetPixelSize(u32)`; `SetPixelSize(0)` and sizes above the sanity limit (2048 px) refused; sizes above `kMaxAtlasPixelSize` are accepted for metrics, layout and outlines (only `Render` refuses them, Day 177); metrics scale with the size (`ascent`, `descent`, `line_height`, `units_per_em`), in 26.6 fixed point converted
      to `f32` pixels once, in one place.
- [ ] **Glyph lookup:** `GlyphId FindGlyph(char32 codepoint)` through the cmap; `.notdef` (id 0) for a missing code point, never an error; invalid code points (surrogates, above 0x10FFFF) map to `.notdef`;
      `HasGlyph(codepoint)`.
- [ ] **Glyph metrics:** advance (horizontal), bearing X/Y, width, height of the bitmap box, per glyph and size; vertical layout is out of scope (recorded).
- [ ] **Kerning:** `f32 Kerning(GlyphId left, GlyphId right)` from the font's kern table / GPOS pair adjustment as far as FreeType exposes it (`FT_Get_Kerning`); a font without kerning gives 0;
      the limits (no GPOS contextual kerning) recorded in the guide.
- [ ] **Outline access (the Phase 16 seam):** `LoadOutline(GlyphId)` returning the contours as points with on/off-curve flags in font units, behind the FreeType boundary; tested by counting contours
      of `o` (2) and `l` (1) and checking the bounding box against the metrics. Nothing else uses it in this phase.
- [ ] **Lifetime:** a `Font` is movable, not copyable; destroying it frees every FreeType object; a `Font` used from two threads without a lock is a documented non-goal (one `Font` per thread or an
      external lock; `FT_Face` is not thread-safe), checked by a tsan test that uses separate fonts from several jobs.
- [ ] **Tests** (`tests/text/font_test.cpp`, phase 14): open the shipped font; metrics at 12/16/24/48 px are monotonic and non-zero; "Hello" glyphs are non-zero; a missing code point gives `.notdef`;
      kerning of "AV" is negative and of "ab" is not; a truncated file, a zero-byte file and a text file are refused with distinct statuses; allocation failure injected at each FreeType allocation
      leaves no leak (the allocation-failure tests of earlier phases are the pattern); asan and tsan clean.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code;
      quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with
      Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the day closes; a day done on one machine files
      "open until the other machine runs it" in `KNOWN-ISSUES.md` and the closing day clears it.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under
      "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a font opens from bytes or a path, answers metrics, glyph ids, per-glyph metrics, kerning and outlines, refuses bad input by status, leaks nothing under failed allocations, and its public header is documented.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
