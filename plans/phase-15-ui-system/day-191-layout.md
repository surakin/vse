# Phase 15 / Day 191 — Layout: Measure, Arrange and the Container Widgets

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Day 190.

The measure/arrange pass and the containers that use it.

## Tasks

- [ ] **Layout protocol:** `Measure(Constraints) -> Size` (min, preferred, max per axis, in logical units, with `kUnbounded`), `Arrange(Rect)`; layout runs top-down once per dirty frame, the second pass only for widgets whose measure depends on the arranged width (wrapping labels, the `WidthForHeight` hook); a cycle guard (two passes at most per frame, a counter if a widget keeps invalidating itself).
- [ ] **Containers:** `BoxLayout` (horizontal and vertical: spacing, padding, per-child weight/flex, alignment start/center/end/stretch, min/preferred sizes honored), `GridLayout` (rows and columns with fixed, auto and weighted tracks, spans), `StackLayout` (children overlaid, aligned), `AnchorLayout` (edges as fractions plus offsets), `Spacer`; `Splitter` is Day 197 (it needs input).
- [ ] **Sizes from content:** a widget reports its preferred size from its content (a label from `text::Layout`, an image from its natural size); layout of a label is cached on (text, font, wrap width, revision) so an unchanged label does not re-layout.
- [ ] **Scale and pixel snapping:** rectangles are arranged in logical units and snapped to whole physical pixels when drawn, so neighbouring widgets never leave a seam or overlap at 1.25x.
- [ ] **Re-layout on resize:** `Surface::Resize(width, height, scale)` marks layout dirty; resizing to the same size does nothing; coalescing of several resizes into one layout per frame is a test (ten resizes between two frames, one layout).
- [ ] **Visibility:** hidden widgets take no space (`collapsed`) or keep it (`hidden`), both tested.
- [ ] **Tests (`tests/ui/layout_test.cpp`):** a table of cases for each container with expected child rectangles (weights, min/max clamping, alignment, spacing, padding, empty container, one child, overflow when the content is larger than the container and the container's own min), grid spans and auto tracks, anchors at several sizes, wrapping label height given width, scale 1x/1.25x/2x snapping, resize coalescing, the cycle guard.
- [ ] **Property test:** random trees of containers with random sizes never produce a negative size, a child outside its parent's content box (unless overflow is allowed), or a non-finite number; seeded, with the seed printed on failure.
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 202 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 202); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** containers lay out their children by the protocol at any size and scale, deterministically, with wrapping text and resize coalescing tested.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
