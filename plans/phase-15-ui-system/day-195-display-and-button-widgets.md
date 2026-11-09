# Phase 15 / Day 195 — Display and Button Widgets

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 191-194.

The first widgets, all theme-driven and tested on the offscreen harness.

## Tasks

- [x] **Display widgets:** `Label` (text, wrap, alignment, ellipsis when truncated, variant e.g. `heading`), `Image` (an `ImageId`, fit modes: stretch, contain, cover, none; tint), `ProgressBar` (value 0..1 and indeterminate animation from the surface clock), `Separator`, `Panel` (background and border from the theme, a container), `Tooltip` data hook on any widget (the popup is Day 198).
- [x] **Buttons:** `Button` (text and/or image, states normal/hover/pressed/focused/disabled, click on release inside, Space/Enter when focused, `OnClick` callback, default-button flag), `ToggleButton`, `CheckBox` (tri-state optional), `RadioButton` + `RadioGroup` (one selected, arrow keys move within the group, group is a container or a name).
- [x] **Callbacks:** a small `ui::Callback<Args...>` (function pointer + user pointer, no `std::function`: the standard-library rule) with `Connect`/`Disconnect` so a widget can have several listeners and disconnect by token.
- [x] **Drawing through the theme only:** no color or metric literal in a widget (a grep-style test over `engine/ui/widgets/*.cpp` fails on hex colors and numeric pixel metrics, listing the line); state transitions repaint only that widget (draw-dirty, not layout-dirty).
- [x] **Accessibility basics within scope:** every interactive widget has a `name` and an optional `description` string (no screen reader support, but present for tests and the Day 203 settings screen).
- [x] **Tests (`tests/ui/basic_widgets_test.cpp`):** each widget's size from content; a click inside releases to `OnClick`, a press then drag out then release does not click, keyboard activation, disabled ignores input and draws with the disabled style, radio group exclusivity and arrow navigation, checkbox cycling, label wrapping and ellipsis, progress at 0, 0.5, 1 and indeterminate frames, theme swap changes the drawn colors without a layout pass (layout counter unchanged), pixel checks with the harness rasterizer for a button in each state.
- [x] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 205 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 205); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [x] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [x] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** labels, images, progress bars, panels, buttons, toggles, checkboxes and radio groups work by mouse and keyboard, draw only from the theme, and are tested on an offscreen surface.

## Notes

- **Where:** `engine/ui/widgets/` (`label.h`, `display.h/.cpp`: `Image`, `ProgressBar`, `Separator`, `Panel`; `buttons.h/.cpp`: `Button`, `ToggleButton`, `CheckBox`, `RadioButton`, `RadioGroup`; `look.h`), `ui/callback.h` (`Callback<Args...>`), `ui/text_run.*` (the cached layout `TextBlock` and the buttons share, with the ellipsis cut), `TextBlock` gained alignment, ellipsis and a theme style kind, `Widget` gained `a description, a tooltip and a role tag, a `radio` style in the default themes; gate `tools/ui_widget_style_check` (hex colors and numbers of 3 or more in `engine/ui/widgets/*.cpp`, with planted fixtures); tests `tests/ui/basic_widgets_test.cpp` (11 cases); the "Widgets" section of `docs/guide/ui.md`.
- **Calls made:** the ellipsis is made by `TextRun::Fit` (the text has no ellipsis option), cut at a cluster boundary. A radio group finds its buttons by a role tag (no run-time type information). `Label` and `ToggleButton` have one out-of-line virtual each so their vtables live in the library (the tests have RTTI, the library has not). Selected radio is the only Tab stop of its group. Images carry their natural size with the id (the id is opaque). The tooltip is data only (the popup is Day 198).
- **Runs:** plain `ran 1662 of 4175 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 1164 of 4175 tests (...asan...)`, tsan `ran 855 of 4175 tests (...tsan...)`; `ctest -L docs` passes; Clang, GCC, unity and non-unity clean.

## Post-implementation code review

`/code-review high`: 4 findings. Fixed: `RadioGroup::Select` of a button outside the group cleared the group's selection (now ignored; test added). Declined: `Callback::Invoke` copies the listener list (clicks are rare); a click bubbling from a capturing child (buttons are leaves); `RadioGroup` walks its subtree on each call (small groups).
