# Phase 14 / Day 178 — Text Layout

**Phase:** [Phase 14 — Font & Text Rendering](../phase-14-font-text-rendering.md)
**Depends on:** Days 176-177; the foundation string module (UTF-8, graphemes: Phase 0 Days 10-12).

UTF-8 in, positioned glyph quads out, with kerning, line breaks and wrapping, and a stable relationship between byte offsets, graphemes and glyphs that Day 179 builds on.

## Tasks

- [x] **Types:** `TextStyle {Font*, pixel_size, color}`, `LayoutOptions {max_width (0 = no wrap), line_height_scale, tab_width_in_spaces, align (Left, Center, Right)}`, `TextLayout`
      (`Vector<PositionedGlyph>`, `Vector<LineInfo>`, total bounds), `PositionedGlyph {glyph, x, y (pen position, baseline origin), advance, source_byte_offset, grapheme_index, line}`.
- [x] **Pipeline:** `Layout(StringView utf8, style, options) -> TextLayout`: decode UTF-8 (invalid sequences become U+FFFD, counted, never a crash), split into grapheme clusters with the foundation string module,
      map each code point to a glyph (a cluster of several code points yields one `PositionedGlyph` per code point with zero advance for combining marks and ZWJ/variation selectors — recorded as a no-shaping
      limitation), apply kerning between adjacent base glyphs, advance the pen.
- [x] **Lines:** `\n` breaks; `\r\n` is one break; tabs advance to the next multiple of `tab_width`; soft wrap at `max_width` breaks at the last whitespace, else between grapheme clusters when a word is wider than the
      line (never inside a cluster); trailing whitespace does not count towards the width; empty lines keep the line height.
- [x] **Metrics:** per-line ascent/descent from the font, baseline positions, total width/height; alignment applied per line; coordinates are pixels with the origin at the top-left of the first line and y down
      (as the overlay pass draws).
- [x] **Determinism and cost:** the same input gives byte-identical output; layout allocates only from its result vectors (reserved up front from the byte length); a 10,000-character string lays out in well under a
      frame (measured on Day 183, no threshold asserted in tests).
- [x] **Tests** (`tests/text/layout_test.cpp`, phase 14): "Hello, world!" advances sum to the font's advances plus kerning; kerned pair "AV" is closer than the unkerned sum; wrapping at a width breaks at spaces
      and never inside "e" + combining acute; a long word wraps between clusters; ZWJ family emoji (a cluster of seven code points) keeps one grapheme index and draws its glyphs without a crash (`.notdef` when the
      font lacks them); invalid UTF-8 gives U+FFFD and a count; empty string gives an empty layout with one line of height; CRLF; tabs; alignment offsets; bytes offsets are monotonic and cover the input exactly.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code;
      quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with
      Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the day closes; a day done on one machine files
      "open until the other machine runs it" in `KNOWN-ISSUES.md` and the closing day clears it.
- [x] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed.
- [x] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under
      "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a UTF-8 string lays out into positioned glyphs and lines with kerning, wrapping and alignment, every glyph knows its byte offset and grapheme index, and the layout tests (including the combining-mark and ZWJ cases) pass.

## Notes

- New: `engine/text/layout.h/.cpp`: `TextStyle`, `LayoutOptions`, `Align`, `PositionedGlyph`, `ClusterInfo`, `LineInfo`, `TextLayout`, `Layout()`; `Status::Of` factory (the atlas and layout share it).
- Beyond the day's type list: `TextLayout` also carries `clusters` (byte range, line, x, width, glyph range), because the cursor (Day 179) moves by cluster and the byte ranges must tile the input.
- Decisions: combining marks, joiners and variation selectors are drawn at the pen after their base (zero advance; fonts give marks a negative bearing); only space and tab are wrap opportunities; a hard break is a cluster with no glyph; `\\r\\n` is one cluster (UAX 29); a text ending in a break gets an empty last line; trailing spaces overflow without wrapping.
- Tests: `tests/text/layout_test.cpp` (13 cases: advances plus kerning, kerned pair, wrapping and clusters, long word, ZWJ family, invalid UTF-8, empty and breaks, tabs, alignment, determinism and tiling, option validation, a 4001-line text).
- Runs on Linux (Clang), reduced: `--affected` plain `ran 1449 of 3726 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 90 s` (one failure: the known X11 session flake `X11Window::Create opens a mapped window on the real X server`); asan `ran 1037 of 3726 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 51 s`; tsan `ran 746 of 3726 tests (phases: all; flavors: tsan; display: included; crash tests: excluded) in 9 s`. `ctest -L docs` 6 of 6. GCC and Clang non-unity builds of `text` and `text_tests` clean; tests pass. Windows: still open (`KNOWN-ISSUES.md`).

## Post-implementation code review

`/code-review high`: 5 findings, all fixed.

1. A tab stop of 0 (a font whose space has no advance) divided by zero and cast infinity to an integer. Fixed: a tab is one em then. No regression test: the shipped font has a normal space and no font with a zero-width space is available; the guard is a single comparison.
2. The per-line exact `TryReserve` made the line pass quadratic. Fixed: one reservation up front; the new `A long text with many lines lays out` test (4001 lines) exercises it.
3. `line_height_scale` and `max_width` were unvalidated. Fixed: refused with `InvalidArgument`; test `Layout refuses nonsense options` fails without the check.
4. `std::char_traits` and a `(void)` cast in the test. Fixed.
5. Break opportunities undocumented. Fixed in the header and the guide.
