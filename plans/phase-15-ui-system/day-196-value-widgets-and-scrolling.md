# Phase 15 / Day 196 — Value Widgets and Scrolling: Slider, Spin Box, Scrollbar, Scroll View, List Box

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Day 195.

The widgets that hold a number or an offset, and the scrolling machinery text fields and lists share.

## Tasks

- [ ] **`Slider`:** horizontal and vertical, min/max/step, value changed callback while dragging and on release, keyboard (arrows, Page, Home/End), wheel optional, click on the track jumps by a page or to the point (theme/variant option), pointer capture while dragging, ticks optional.
- [ ] **`ScrollBar`:** thumb length proportional to the view/content ratio with a minimum thumb size, track click pages, thumb drag with capture, arrow buttons optional, auto-hide variant; value in content units.
- [ ] **`ScrollView`:** one child larger than the view; vertical and horizontal bars shown when needed (policy: auto, always, never); wheel and bar scrolling clamp to the content; `ScrollTo(rect)` to bring a child into view (used by focus and the list box); clips its child's drawing and hit testing to the viewport; scroll inertia is not implemented (documented).
- [ ] **`ListBox`:** items from a model interface (`ItemCount`, `ItemText(i)` or a custom item widget factory), single and multi selection (Ctrl/Shift click), keyboard navigation with type-ahead by first letter, selection changed callback, virtualization: only the visible items are built/drawn, so 100,000 items cost the same per frame as 100.
- [ ] **`SpinBox`:** a numeric text display with up/down buttons, step, range, format; typing is Day 197's text field embedded (the spin box is finished there; today it supports buttons, wheel and arrows and exposes a hook for the text field).
- [ ] **Value types and ranges:** `f64` with an optional integer mode; clamp and snap-to-step exact for integer steps (no accumulation drift: value = min + round((v-min)/step)*step); ranges can come from settings metadata (Day 203) so the types accept a `RangePolicy`-like clamp/reject choice.
- [ ] **Tests (`tests/ui/value_widgets_test.cpp`, `scroll_test.cpp`):** slider value at drag positions, snap exactness over 1,000 steps, keyboard, wheel; scrollbar thumb geometry for several content/view ratios including content smaller than view; scroll view clamps, wheel bubbling from an inner non-scrollable widget, `ScrollTo`; list box selection rules, type-ahead, virtualization (item widgets built for 100,000 items stays under a fixed count, measured), scroll to the selected item; allocation test: scrolling a long list for 100 frames allocates nothing in steady state.
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 205 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 205); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** sliders, scrollbars, scroll views and virtualized list boxes work by mouse, wheel and keyboard, with exact snapping and a per-frame cost independent of the item count.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
