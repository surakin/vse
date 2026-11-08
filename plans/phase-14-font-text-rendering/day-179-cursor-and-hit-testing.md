# Phase 14 / Day 179 — Cursor Placement and Hit-Testing

**Phase:** [Phase 14 — Font & Text Rendering](../phase-14-font-text-rendering.md)
**Depends on:** Day 178.

What the Phase 15 text field will call: move by grapheme, map a byte offset to a position and a position to a byte offset, and give selection rectangles.

## Tasks

- [x] **Offsets:** `PositionOfOffset(layout, byte_offset) -> CursorPosition {x, y, line, height}` for any offset that is a grapheme boundary; an offset inside a cluster snaps to the cluster's start (documented),
      and past-the-end is the end of the last line.
- [x] **Hit test:** `OffsetAtPoint(layout, x, y) -> byte_offset` choosing the nearest grapheme boundary (the half of the cluster the point is in decides which side); a point above/below the text clamps to the
      first/last line; left of a line is its start, right of it its end.
- [x] **Movement:** `NextBoundary(layout, offset)`, `PreviousBoundary(...)` by grapheme (combining marks and ZWJ sequences are one step), `LineStart/LineEnd(offset)`, `LineAbove/LineBelow(offset, preferred_x)`
      keeping the preferred column across lines of different length; word movement is not in scope (recorded for Phase 15).
- [x] **Selection:** `SelectionRects(layout, begin, end) -> Vector<Rect>` one rectangle per line covered, merging adjacent clusters; order-independent in `begin`/`end`.
- [x] **Round trip property:** for every grapheme boundary offset `o` of a set of strings, `OffsetAtPoint(PositionOfOffset(o)) == o`; for every point, `PositionOfOffset(OffsetAtPoint(p))` is within half a
      cluster width of `p` on that line.
- [x] **Tests** (`tests/text/cursor_test.cpp`, phase 14): ASCII, accents as precomposed and as combining marks (both move one step), flag emoji (regional-indicator pairs), ZWJ family, Hangul jamo sequences if
      the string module classifies them, multiple lines with wrap, empty string, a string of only combining marks, `NextBoundary` walk covers every offset exactly once and ends at the length.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code;
      quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with
      Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the day closes; a day done on one machine files
      "open until the other machine runs it" in `KNOWN-ISSUES.md` and the closing day clears it.
- [x] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed.
- [x] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under
      "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the cursor and hit-test functions are documented and tested over ASCII, combining marks, regional indicators, ZWJ sequences and wrapped lines, and the round-trip property holds for all of them.

## Notes

- New: `engine/text/cursor.h/.cpp`: `PositionOfOffset`, `OffsetAtPoint`, `NextBoundary`, `PreviousBoundary`, `LineStart`, `LineEnd`, `LineAbove`, `LineBelow`, `SelectionRects`; `CursorPosition`, `SelectionRect`. `ClusterInfo` gained `kind` (`ClusterKind`: Normal, Space, Tab, Break), which the line-end rule needs.
- Decisions: no caret affinity (a soft-wrap offset belongs to the later line); the end of a wrapped line is before its trailing spaces, or the next line's start when it has none; a lone combining mark (a cluster with no width) shares its x with the next boundary, so only the earlier one is clickable; empty parts of a line (blank lines, line starts) give no selection rectangle; a break is not painted.
- Tests: `tests/text/cursor_test.cpp` (10 cases): walk covers every cluster once for ten strings (ASCII, precomposed and combining accents, two flags, ZWJ family, Hangul jamo, several lines with CRLF, wrapped, empty, only combining marks, trailing break) and `PreviousBoundary` retraces it; one-step cases; positions; hit testing and clamping; the round trip over every boundary and over a grid of points, unwrapped and wrapped; line start/end and vertical movement with a preferred column; soft wraps; selection rectangles; aligned layouts; extreme and NaN points.
- Runs on Linux (Clang), reduced: `--affected` plain `ran 1459 of 3756 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 91 s` (one failure: the known X11 session flake); asan `ran 1047 of 3756 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 50 s`; tsan `ran 756 of 3756 tests (phases: all; flavors: tsan; display: included; crash tests: excluded) in 10 s`. `ctest -L docs` 6 of 6. GCC and Clang non-unity builds clean; tests pass. Windows: still open (`KNOWN-ISSUES.md`).

## Post-implementation code review

`/code-review high`: 3 findings, all fixed.

1. `OffsetAtPoint` cast `y / height` to an integer before clamping (undefined for huge or infinite y). Fixed: compared as a float first; NaN and negative y stay on the first line; test `Extreme and not-a-number points are clamped, not converted`.
2. Offsets are 32-bit but `Layout` took any length. Fixed: text over 4 GiB is refused with `InvalidArgument` (no test: it would need a 4 GiB string).
3. Aligned layouts and extreme points were untested. Fixed: the two new tests.
