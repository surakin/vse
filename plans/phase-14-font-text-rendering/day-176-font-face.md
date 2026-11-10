# Phase 14 / Day 176 — Font Faces: Loading, Metrics, Glyph Lookup, Kerning

**Phase:** [Phase 14 — Font & Text Rendering](../phase-14-font-text-rendering.md)
**Depends on:** Day 175.

A `text::Font` that loads a font file through the file layer and answers every question layout needs, with errors as values.

## Tasks

- [x] **`FontLibrary`:** the owner of one FreeType library and its counting allocator hook (from `ProbeLibrary`'s code), created once and shared by the `Font`s opened from it (design spec section 2); not copyable, movable, destroyed after its fonts (a `Font` holds a reference; the destructor order is tested); allocation statistics readable.
- [x] **Loading:** `Font::Open(ByteSpan data, FontOptions)` (the caller reads the file through `io::File`; the font copies or keeps the bytes as the spec decided — FreeType needs the memory alive,
      so the `Font` owns them) and `OpenFile(io::Path)` convenience; face index; errors as `text::Status` (not found, not a font, unsupported format, out of memory) with a message that names the cause.
- [x] **Sizing:** `SetPixelSize(u32)`; `SetPixelSize(0)` and sizes above the sanity limit (2048 px) refused; sizes above `kMaxAtlasPixelSize` are accepted for metrics, layout and outlines (only `Render` refuses them, Day 177); metrics scale with the size (`ascent`, `descent`, `line_height`, `units_per_em`), in 26.6 fixed point converted
      to `f32` pixels once, in one place.
- [x] **Glyph lookup:** `GlyphId FindGlyph(char32 codepoint)` through the cmap; `.notdef` (id 0) for a missing code point, never an error; invalid code points (surrogates, above 0x10FFFF) map to `.notdef`;
      `HasGlyph(codepoint)`.
- [x] **Glyph metrics:** advance (horizontal), bearing X/Y, width, height of the bitmap box, per glyph and size; vertical layout is out of scope (recorded).
- [x] **Kerning:** `f32 Kerning(GlyphId left, GlyphId right)` from the font's kern table / GPOS pair adjustment as far as FreeType exposes it (`FT_Get_Kerning`); a font without kerning gives 0;
      the limits (no GPOS contextual kerning) recorded in the guide.
- [x] **Outline access (the Phase 25 seam):** `LoadOutline(GlyphId)` returning the contours as points with on/off-curve flags in font units, behind the FreeType boundary; tested by counting contours
      of `o` (2) and `l` (1) and checking the bounding box against the metrics. Nothing else uses it in this phase.
- [x] **Lifetime:** a `Font` is movable, not copyable; destroying it frees every FreeType object; a `Font` used from two threads without a lock is a documented non-goal (one `Font` per thread or an
      external lock; `FT_Face` is not thread-safe), checked by a tsan test that uses separate fonts from several jobs.
- [x] **Tests** (`tests/text/font_test.cpp`, phase 14): open the shipped font; metrics at 12/16/24/48 px are monotonic and non-zero; "Hello" glyphs are non-zero; a missing code point gives `.notdef`;
      kerning of "AV" is negative and of "ab" is not; a truncated file, a zero-byte file and a text file are refused with distinct statuses; allocation failure injected at each FreeType allocation
      leaves no leak (the allocation-failure tests of earlier phases are the pattern); asan and tsan clean.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code;
      quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with
      Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the day closes; a day done on one machine files
      "open until the other machine runs it" in `KNOWN-ISSUES.md` and the closing day clears it.
- [x] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed.
- [x] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under
      "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a font opens from bytes or a path, answers metrics, glyph ids, per-glyph metrics, kerning and outlines, refuses bad input by status, leaks nothing under failed allocations, and its public header is documented.

## Notes

- `engine/text/font.h` (public API, documented, strict labels) and `font_freetype.cpp` (the only FreeType file): `FontLibrary` (one FreeType library, counting allocator, `FailAllocationsAfter` test aid, non-movable;
  its destructor aborts while a font is open), `Font` (open from bytes or path, pixel size, metrics, glyph lookup, glyph metrics, `kern`-table kerning, unscaled outlines), `text::Status`.
  `ProbeLibrary` now runs on `FontLibrary`.
- Decisions: `FontLibrary` is not movable (fonts point at it), so the task's "movable" became "non-movable, fonts hold a pointer and the destructor order is checked"; truncated sfnt files are caught by a table-bounds
  check before FreeType (FreeType loads tables lazily, so a cut-short file opens and fails later); a refused allocation FreeType swallows while opening makes `Open` fail with `OutOfMemory`;
  non-font data with no known signature that FreeType rejects as short/malformed is `NotAFont`.
- Found by the allocation-failure test: FreeType 2.15.0's auto-hinter crashes when an allocation fails during the first hinted glyph load (`KNOWN-ISSUES.md`); the injection test covers open and unhinted outlines.
- Tests: `tests/text/font_test.cpp` (15 test cases in the binary with the library tests; 1835 assertions), including an allocation-failure sweep of 400 failure points, a 4-thread test with separate libraries, and the bad-input statuses.
- Guide: `docs/guide/text.md` created (listed in `docs/guide/README.md`).
- Runs on Linux (Clang): `--affected` plain: `ran 1424 of 3651 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 89 s` (one failure, `X11Window::Create opens a mapped window on the real X server`,
  the session-state flake already in `KNOWN-ISSUES.md`); asan: `ran 1012 of 3651 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 51 s`; tsan: `ran 721 of 3651 tests (phases: all; flavors: tsan; display: included; crash tests: excluded) in 11 s`.
  Reduced runs. `ctest -L docs` 6 of 6. Clang non-unity and GCC builds of `text` and `text_tests` clean (GCC found a `-Wsubobject-linkage` problem, fixed by naming the counters' namespace) and pass.
- Windows: still open (filed on Day 175).

## Post-implementation code review

`/code-review high`: 7 findings, 5 fixed, 2 declined with reasons.

1. Const methods mutate the shared face, so concurrent reads are unsafe. Fixed: the class documentation says so (use one `Font` per thread or a lock).
2. `OpenFile` copied the file bytes twice. Fixed: the read buffer is moved into the font (`Font::OpenOwned`); a test checks both open paths agree.
3. The FreeType auto-hinter crash was not filed. Fixed: `KNOWN-ISSUES.md` entry.
4. The "allocation refused during open" check is library-wide. Declined: a library is documented as single-thread-at-a-time (`FontLibrary` class comment), so no other thread allocates during an `Open`.
5. `OpenFile` reports every read failure as `NotFound`. Declined: the cause is in `Status::io_error`, which the header documents; a second error enum would duplicate `io::Error`.
6. The metrics box ignored the fractional left bearing. Fixed: the box runs from floor to ceiling on every edge; regression test `A glyph's pixel box contains its whole outline` (it failed with the old rounding for heights and widths in the 13/17/33 px sweep before the box change was combined with the loosened hinting tolerance).
7. `FailAllocationsAfter` sits in the public class. Fixed as documentation only: labelled a test aid; kept public because tests in another target need it.
