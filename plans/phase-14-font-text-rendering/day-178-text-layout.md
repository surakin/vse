# Phase 14 / Day 178 — Text Layout

**Phase:** [Phase 14 — Font & Text Rendering](../phase-14-font-text-rendering.md)
**Depends on:** Days 176-177; the foundation string module (UTF-8, graphemes: Phase 0 Days 10-12).

UTF-8 in, positioned glyph quads out, with kerning, line breaks and wrapping, and a stable relationship between byte offsets, graphemes and glyphs that Day 179 builds on.

## Tasks

- [ ] **Types:** `TextStyle {Font*, pixel_size, color}`, `LayoutOptions {max_width (0 = no wrap), line_height_scale, tab_width_in_spaces, align (Left, Center, Right)}`, `TextLayout`
      (`Vector<PositionedGlyph>`, `Vector<LineInfo>`, total bounds), `PositionedGlyph {glyph, x, y (pen position, baseline origin), advance, source_byte_offset, grapheme_index, line}`.
- [ ] **Pipeline:** `Layout(StringView utf8, style, options) -> TextLayout`: decode UTF-8 (invalid sequences become U+FFFD, counted, never a crash), split into grapheme clusters with the foundation string module,
      map each code point to a glyph (a cluster of several code points yields one `PositionedGlyph` per code point with zero advance for combining marks and ZWJ/variation selectors — recorded as a no-shaping
      limitation), apply kerning between adjacent base glyphs, advance the pen.
- [ ] **Lines:** `\n` breaks; `\r\n` is one break; tabs advance to the next multiple of `tab_width`; soft wrap at `max_width` breaks at the last whitespace, else between grapheme clusters when a word is wider than the
      line (never inside a cluster); trailing whitespace does not count towards the width; empty lines keep the line height.
- [ ] **Metrics:** per-line ascent/descent from the font, baseline positions, total width/height; alignment applied per line; coordinates are pixels with the origin at the top-left of the first line and y down
      (as the overlay pass draws).
- [ ] **Determinism and cost:** the same input gives byte-identical output; layout allocates only from its result vectors (reserved up front from the byte length); a 10,000-character string lays out in well under a
      frame (measured on Day 183, no threshold asserted in tests).
- [ ] **Tests** (`tests/text/layout_test.cpp`, phase 14): "Hello, world!" advances sum to the font's advances plus kerning; kerned pair "AV" is closer than the unkerned sum; wrapping at a width breaks at spaces
      and never inside "e" + combining acute; a long word wraps between clusters; ZWJ family emoji (a cluster of seven code points) keeps one grapheme index and draws its glyphs without a crash (`.notdef` when the
      font lacks them); invalid UTF-8 gives U+FFFD and a count; empty string gives an empty layout with one line of height; CRLF; tabs; alignment offsets; bytes offsets are monotonic and cover the input exactly.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code;
      quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with
      Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the day closes; a day done on one machine files
      "open until the other machine runs it" in `KNOWN-ISSUES.md` and the closing day clears it.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under
      "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a UTF-8 string lays out into positioned glyphs and lines with kerning, wrapping and alignment, every glyph knows its byte offset and grapheme index, and the layout tests (including the combining-mark and ZWJ cases) pass.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
