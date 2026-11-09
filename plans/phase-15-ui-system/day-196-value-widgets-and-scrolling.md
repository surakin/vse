# Phase 15 / Day 196 — Value Widgets and Scrolling: Slider, Spin Box, Scrollbar, Scroll View, List Box

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Day 195.

The widgets that hold a number or an offset, and the scrolling machinery text fields and lists share.

## Tasks

- [x] **`Slider`:** horizontal and vertical, min/max/step, value changed callback while dragging and on release, keyboard (arrows, Page, Home/End), wheel optional, click on the track jumps by a page or to the point (theme/variant option), pointer capture while dragging, ticks optional.
- [x] **`ScrollBar`:** thumb length proportional to the view/content ratio with a minimum thumb size, track click pages, thumb drag with capture, arrow buttons optional, auto-hide variant; value in content units.
- [x] **`ScrollView`:** one child larger than the view; vertical and horizontal bars shown when needed (policy: auto, always, never); wheel and bar scrolling clamp to the content; `ScrollTo(rect)` to bring a child into view (used by focus and the list box); clips its child's drawing and hit testing to the viewport; scroll inertia is not implemented (documented).
- [x] **`ListBox`:** items from a model interface (`ItemCount`, `ItemText(i)` or a custom item widget factory), single and multi selection (Ctrl/Shift click), keyboard navigation with type-ahead by first letter, selection changed callback, virtualization: only the visible items are built/drawn, so 100,000 items cost the same per frame as 100.
- [x] **`SpinBox`:** a numeric text display with up/down buttons, step, range, format; typing is Day 197's text field embedded (the spin box is finished there; today it supports buttons, wheel and arrows and exposes a hook for the text field).
- [x] **Value types and ranges:** `f64` with an optional integer mode; clamp and snap-to-step exact for integer steps (no accumulation drift: value = min + round((v-min)/step)*step); ranges can come from settings metadata (Day 203) so the types accept a `RangePolicy`-like clamp/reject choice.
- [x] **Tests (`tests/ui/value_widgets_test.cpp`, `scroll_test.cpp`):** slider value at drag positions, snap exactness over 1,000 steps, keyboard, wheel; scrollbar thumb geometry for several content/view ratios including content smaller than view; scroll view clamps, wheel bubbling from an inner non-scrollable widget, `ScrollTo`; list box selection rules, type-ahead, virtualization (item widgets built for 100,000 items stays under a fixed count, measured), scroll to the selected item; allocation test: scrolling a long list for 100 frames allocates nothing in steady state.
- [x] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 205 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 205); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [x] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [x] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** sliders, scrollbars, scroll views and virtualized list boxes work by mouse, wheel and keyboard, with exact snapping and a per-frame cost independent of the item count.

## Notes

- **Where:** `engine/ui/widgets/` (`range.h`: `ValueRange`; `scroll.h/.cpp`: `ScrollBar`, `ScrollView`; `value_widgets.h/.cpp`: `Slider`, `SpinBox`; `list_box.h/.cpp`: `ListModel`, `ListBox`), `Widget::BringToFront` and `OnDescendantFocused`, `Surface` (focus hook, event-buffer swap, kept canvas), `DrawListCanvas::Rebind`, `TextRun` (text in a `Vector<char>`), `slider` and `menu` theme styles; tests `tests/ui/value_widgets_test.cpp`, `scroll_test.cpp`; the "Value widgets and scrolling" section of `docs/guide/ui.md`.
- **Calls made:** the snap is `min + k * step` with one rounding (no accumulation); a maximum off the grid is not reached. The list box has uniform row heights (so index and range are arithmetic) and a ring of cached text runs sized to the visible rows plus one. Custom item widgets are not built (text items from a model only; a factory can come with the menus on Day 198). The `SpinBox` draws its own text today; Day 197 embeds the text field.
- **Allocation test:** the plan asks for no allocation while scrolling a long list for 100 frames. The test scrolls within the rows already laid out and finds zero allocations (after the surface kept its canvas and event buffers; before, every frame made the clip stack). A row laid out for the first time still allocates inside `text::Layout`; filed in `KNOWN-ISSUES.md`. Skipped under sanitizers (they own `operator new`).
- **Runs:** plain `ran 1677 of 4220 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 1179 of 4220 tests (...asan...)`, tsan `ran 870 of 4220 tests (...tsan...)`; `ctest -L docs` passes; Clang, GCC, unity and non-unity clean.

## Post-implementation code review

`/code-review high`: 6 findings. Fixed: (1) nested scroll views: the inner view's focus scroll now re-arranges at once, so the outer view reads the widget's real position (test fails without the fix); (2) `ListBox::ModelChanged` kept the selection when the count was unchanged (now always cleared; test added); (3) clicking the only selected item again reported a selection change (test added). Declined: `SetMode(Single)` clears a multi-selection (rare, simple); Space does not toggle in multiple mode (Ctrl+click is the toggle); the allocation test covers rows already laid out (new rows allocate in the text module, filed).
