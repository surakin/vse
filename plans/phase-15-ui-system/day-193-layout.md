# Phase 15 / Day 193 — Layout: Measure, Arrange and the Container Widgets

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Day 192.

The measure/arrange pass and the containers that use it.

## Tasks

- [x] **Layout protocol:** `Measure(Constraints) -> Size` (min, preferred, max per axis, in logical units, with `kUnbounded`), `Arrange(Rect)`; layout runs top-down once per dirty frame, the second pass only for widgets whose measure depends on the arranged width (wrapping labels, the `WidthForHeight` hook); a cycle guard (two passes at most per frame, a counter if a widget keeps invalidating itself).
- [x] **Containers:** `BoxLayout` (horizontal and vertical: spacing, padding, per-child weight/flex, alignment start/center/end/stretch, min/preferred sizes honored), `GridLayout` (rows and columns with fixed, auto and weighted tracks, spans), `StackLayout` (children overlaid, aligned), `AnchorLayout` (edges as fractions plus offsets), `Spacer`; `Splitter` is Day 199 (it needs input).
- [x] **Sizes from content:** a widget reports its preferred size from its content (a label from `text::Layout`, an image from its natural size); layout of a label is cached on (text, font, wrap width, revision) so an unchanged label does not re-layout.
- [x] **Scale and pixel snapping:** rectangles are arranged in logical units and snapped to whole physical pixels when drawn, so neighbouring widgets never leave a seam or overlap at 1.25x.
- [x] **Re-layout on resize:** `Surface::Resize(width, height, scale)` marks layout dirty; resizing to the same size does nothing; coalescing of several resizes into one layout per frame is a test (ten resizes between two frames, one layout).
- [x] **Visibility:** hidden widgets take no space (`collapsed`) or keep it (`hidden`), both tested.
- [x] **Tests (`tests/ui/layout_test.cpp`):** a table of cases for each container with expected child rectangles (weights, min/max clamping, alignment, spacing, padding, empty container, one child, overflow when the content is larger than the container and the container's own min), grid spans and auto tracks, anchors at several sizes, wrapping label height given width, scale 1x/1.25x/2x snapping, resize coalescing, the cycle guard.
- [x] **Property test:** random trees of containers with random sizes never produce a negative size, a child outside its parent's content box (unless overflow is allowed), or a non-finite number; seeded, with the seed printed on failure.
- [x] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 205 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 205); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [x] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [x] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** containers lay out their children by the protocol at any size and scale, deterministically, with wrapping text and resize coalescing tested.

## Notes

- **Where:** `engine/ui/layout_types.h` (`Constraints`, `Align`, `AnchorSpec`, `LayoutProps`), `layout.{h,cpp}` (`BoxLayout`, `StackLayout`, `AnchorLayout`, `GridLayout`, `Spacer`), `text_block.{h,cpp}` (`TextBlock`, the base of Day 195's label), `Widget::Measure/Arrange(rect)/MeasureTotal`, `Surface` two-pass layout and `LayoutOverruns`; tests `tests/ui/layout_test.cpp` (14 cases, 1196 assertions, including a 200-seed property test); the "Layout" section of `docs/guide/ui.md`.
- **Calls made:** `Measure` returns one preferred size (not min/preferred/max): the minimum and maximum are `LayoutProps` the container reads, so a widget cannot make them depend on content. Explicit `width`/`height` are preferences; `Align::Stretch` (the default) fills the cell within `min`/`max` and ignores them. The second layout pass is the surface re-running when a widget invalidates layout during `Arrange`. A plain parent keeps its children's bounds unless a child has `fill_parent`. `Surface::LayoutGeneration` invalidates every cached measurement on any layout change. `Widget`'s virtuals stay inline (key-function note). The grid caps tracks at 256 and clamps wild cell indices.
- **Not done:** spanning children do not size auto tracks; non-finite props are not sanitised; the measure cache holds one entry per widget (the 14-deep nesting test is fast).
- **Runs (reduced, session locked):** plain `ran 1629 of 4084 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (the two known locked-session X11 failures), asan `ran 1135 of 4084 tests (...asan...)`, tsan `ran 826 of 4084 tests (...tsan...)`; `ctest -L docs` passes; Clang, GCC, unity and non-unity clean.

## Post-implementation code review

Run with `/code-review high`: 5 findings.

- **Fixed:** a grid column, row or span of billions allocated that many tracks (now capped at 256, indices clamped; regression test with 4000000000); a wrapping text block measured below one pixel got `max_width` 0 (= no wrapping), now at least 1.
- **Declined:** non-finite layout props are not sanitised (a caller bug; the draw list culls NaN rectangles); single-entry measure cache (fast at 14 levels); spanning children ignored for auto tracks (documented).
