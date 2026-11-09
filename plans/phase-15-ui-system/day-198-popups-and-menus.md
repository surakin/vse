# Phase 15 / Day 198 — Popups, Menus, Combobox and Tooltips

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 192, 194, 195, 196.

Everything that overlays other content: the popup layer, menus, the combobox dropdown, context menus, tooltips.

## Tasks

- [x] **Popup layer:** `Surface::OpenPopup(widget, anchor_rect, placement)` puts a widget in the top layer (draw layer from Day 189), positioned below/above/right/left of an anchor and flipped or shifted to stay inside the surface; closes on outside click (the click is consumed or passed through per popup option), Escape, focus loss of the surface, or `ClosePopup`; nested popups form a stack (a submenu closes with its parent); the popup receives events before the tree.
- [x] **`Menu` and `MenuItem`:** items with text, optional icon, a checkmark or radio state, a disabled state, separators, submenus (opened by hover after a delay or by Right arrow, closed by Left or Escape), accelerator text on the right; keyboard navigation (Up/Down skip disabled and separators, wrap; Enter activates; type-ahead by mnemonic), mouse hover highlights and moves the keyboard selection, click activates and closes the whole stack.
- [x] **`MenuBar`:** top-level menus; click opens, hover switches to a neighbour while one is open, Alt or F10 focuses the bar, Alt+letter mnemonics (`&File` marks F), Left/Right move between menus.
- [x] **Context menu:** `Surface::ShowContextMenu(menu, position)` opened from a right-click callback (a widget exposes `OnContextMenu`), placed at the pointer.
- [x] **Accelerators:** `Surface::RegisterAccelerator(KeyChord, Action)` with chords like Ctrl+S, shown in menu items automatically, dispatched after the focus owner declined the key, disabled with their action; conflicts detected at registration.
- [x] **`ComboBox`:** shows the selected item, opens a list popup (a `ListBox` from Day 196, virtualized), keyboard open/close/select, type-ahead, wheel changes the selection when closed (option), editable variant with a `TextField` (the text and the list stay in sync), selection changed callback.
- [x] **Tooltips:** any widget's tooltip text shows after a hover delay from the surface clock, hides on press/leave/scroll, positions near the pointer inside the surface; one tooltip at a time.
- [x] **Lifetime:** a popup whose anchor is destroyed closes; opening a popup while one is opening is serialized; closing during an event handler is deferred (Day 192 rule).
- [x] **Tests (`tests/ui/popup_test.cpp`, `menu_test.cpp`, `combobox_test.cpp`):** placement and flipping at all four surface edges, outside click closes and is consumed or passed through, nested menu stack closes together, submenu delay, keyboard navigation skipping separators and disabled items, mnemonics and Alt, accelerator dispatch order and disable, context menu position clamped, combobox keyboard and wheel, tooltip delay and hide rules, anchor destroyed closes the popup, no focus left behind (focus returns to the previous owner).
- [x] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 205 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 205); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [x] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [x] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** menus with submenus, a menu bar with mnemonics, context menus, comboboxes, accelerators and tooltips work by mouse and keyboard and overlay anything without breaking focus or routing.

## Notes

- Popups: `Surface::OpenPopup(widget, anchor, PopupOptions)` with `PlacePopup` (`ui/popup.h`); stack, outside-click consume/pass-through, owner press, Escape, window focus loss, focus hand-back, orphan closing at dispatch/frame boundaries (a destroyed owner closes its popup).
- Tooltips live in their own layer (`Surface::TooltipLayer`) above the popups; the delay counts from the last pointer movement, and a press, key, wheel turn or popup hides the bubble until the pointer moves.
- Menus: `Menu` model + `MenuView` popup; `MenuBar` with F10 / lone Alt / Alt+letter via `Surface::SetMenuKeyTarget`; the bar borrows the focus (`SaveFocus` / `RestoreSavedFocus`) and the surface gives it back to the previous owner when its menu closes.
- `ListBox` gained `SetActivateOnClick` and `SetVisibleRows` for the drop-down; `ComboBox` embeds a hidden `TextField` for the editable variant.
- Plan deviations: the context menu is `Widget::SetContextHandler` (a function, not an `OnContextMenu` callback list); menu items have no icon; accelerator text is shown for an item that names its accelerator id (`SetItemAccelerator`) rather than automatically.
- Limits filed in KNOWN-ISSUES.md.

## Runs

- plain: `ran 1745 of 4424 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`
- asan: `ran 1247 of 4424 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`
- tsan: `ran 938 of 4424 tests (phases: all; flavors: tsan; display: included; crash tests: excluded)`
- `ctest -L docs` passes; the five gates pass; Clang and GCC, unity on and off, build clean.

## Post-implementation code review

`/code-review high` over the day's diff: 7 findings; 4 fixed, 3 declined or left.

- Fixed: a menu opened by Alt+letter stranded the keyboard focus on the bar after it closed (regression test: "choosing from a menu opened by Alt+letter gives the focus back", which failed before the fix).
- Fixed: a lone Alt key-up reached the focused bar and the menu key target both, toggling twice (test "a lone Alt toggles the bar", failed before).
- Fixed: a tooltip hidden by a press came back on release without pointer movement (test "a press, a wheel turn and a popup hide the tooltip", failed before).
- Fixed: `const_cast` in `ComboBox::ButtonWidth`.
- Declined: the editable combo box scans the model per keystroke (a few ms for 100,000 items).
- Declined: a closed popup lives until the end of the dispatch (the Day 192 deferral rule).
- Declined: a showing tooltip is not refreshed when its text changes.
