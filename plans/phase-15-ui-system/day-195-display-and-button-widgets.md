# Phase 15 / Day 195 — Display and Button Widgets

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 191-194.

The first widgets, all theme-driven and tested on the offscreen harness.

## Tasks

- [ ] **Display widgets:** `Label` (text, wrap, alignment, ellipsis when truncated, variant e.g. `heading`), `Image` (an `ImageId`, fit modes: stretch, contain, cover, none; tint), `ProgressBar` (value 0..1 and indeterminate animation from the surface clock), `Separator`, `Panel` (background and border from the theme, a container), `Tooltip` data hook on any widget (the popup is Day 198).
- [ ] **Buttons:** `Button` (text and/or image, states normal/hover/pressed/focused/disabled, click on release inside, Space/Enter when focused, `OnClick` callback, default-button flag), `ToggleButton`, `CheckBox` (tri-state optional), `RadioButton` + `RadioGroup` (one selected, arrow keys move within the group, group is a container or a name).
- [ ] **Callbacks:** a small `ui::Callback<Args...>` (function pointer + user pointer, no `std::function`: the standard-library rule) with `Connect`/`Disconnect` so a widget can have several listeners and disconnect by token.
- [ ] **Drawing through the theme only:** no color or metric literal in a widget (a grep-style test over `engine/ui/widgets/*.cpp` fails on hex colors and numeric pixel metrics, listing the line); state transitions repaint only that widget (draw-dirty, not layout-dirty).
- [ ] **Accessibility basics within scope:** every interactive widget has a `name` and an optional `description` string (no screen reader support, but present for tests and the Day 203 settings screen).
- [ ] **Tests (`tests/ui/basic_widgets_test.cpp`):** each widget's size from content; a click inside releases to `OnClick`, a press then drag out then release does not click, keyboard activation, disabled ignores input and draws with the disabled style, radio group exclusivity and arrow navigation, checkbox cycling, label wrapping and ellipsis, progress at 0, 0.5, 1 and indeterminate frames, theme swap changes the drawn colors without a layout pass (layout counter unchanged), pixel checks with the harness rasterizer for a button in each state.
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 205 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 205); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** labels, images, progress bars, panels, buttons, toggles, checkboxes and radio groups work by mouse and keyboard, draw only from the theme, and are tested on an offscreen surface.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
