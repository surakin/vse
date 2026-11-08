# Phase 14 / Day 179 — Cursor Placement and Hit-Testing

**Phase:** [Phase 14 — Font & Text Rendering](../phase-14-font-text-rendering.md)
**Depends on:** Day 178.

What the Phase 15 text field will call: move by grapheme, map a byte offset to a position and a position to a byte offset, and give selection rectangles.

## Tasks

- [ ] **Offsets:** `PositionOfOffset(layout, byte_offset) -> CursorPosition {x, y, line, height}` for any offset that is a grapheme boundary; an offset inside a cluster snaps to the cluster's start (documented),
      and past-the-end is the end of the last line.
- [ ] **Hit test:** `OffsetAtPoint(layout, x, y) -> byte_offset` choosing the nearest grapheme boundary (the half of the cluster the point is in decides which side); a point above/below the text clamps to the
      first/last line; left of a line is its start, right of it its end.
- [ ] **Movement:** `NextBoundary(layout, offset)`, `PreviousBoundary(...)` by grapheme (combining marks and ZWJ sequences are one step), `LineStart/LineEnd(offset)`, `LineAbove/LineBelow(offset, preferred_x)`
      keeping the preferred column across lines of different length; word movement is not in scope (recorded for Phase 15).
- [ ] **Selection:** `SelectionRects(layout, begin, end) -> Vector<Rect>` one rectangle per line covered, merging adjacent clusters; order-independent in `begin`/`end`.
- [ ] **Round trip property:** for every grapheme boundary offset `o` of a set of strings, `OffsetAtPoint(PositionOfOffset(o)) == o`; for every point, `PositionOfOffset(OffsetAtPoint(p))` is within half a
      cluster width of `p` on that line.
- [ ] **Tests** (`tests/text/cursor_test.cpp`, phase 14): ASCII, accents as precomposed and as combining marks (both move one step), flag emoji (regional-indicator pairs), ZWJ family, Hangul jamo sequences if
      the string module classifies them, multiple lines with wrap, empty string, a string of only combining marks, `NextBoundary` walk covers every offset exactly once and ends at the length.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code;
      quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with
      Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the day closes; a day done on one machine files
      "open until the other machine runs it" in `KNOWN-ISSUES.md` and the closing day clears it.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under
      "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the cursor and hit-test functions are documented and tested over ASCII, combining marks, regional indicators, ZWJ sequences and wrapped lines, and the round-trip property holds for all of them.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
