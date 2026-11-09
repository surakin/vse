# Phase 15 / Day 196 — Popups, Menus, Combobox and Tooltips

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 190, 192, 193, 194.

Everything that overlays other content: the popup layer, menus, the combobox dropdown, context menus, tooltips.

## Tasks

- [ ] **Popup layer:** `Surface::OpenPopup(widget, anchor_rect, placement)` puts a widget in the top layer (draw layer from Day 187), positioned below/above/right/left of an anchor and flipped or shifted to stay inside the surface; closes on outside click (the click is consumed or passed through per popup option), Escape, focus loss of the surface, or `ClosePopup`; nested popups form a stack (a submenu closes with its parent); the popup receives events before the tree.
- [ ] **`Menu` and `MenuItem`:** items with text, optional icon, a checkmark or radio state, a disabled state, separators, submenus (opened by hover after a delay or by Right arrow, closed by Left or Escape), accelerator text on the right; keyboard navigation (Up/Down skip disabled and separators, wrap; Enter activates; type-ahead by mnemonic), mouse hover highlights and moves the keyboard selection, click activates and closes the whole stack.
- [ ] **`MenuBar`:** top-level menus; click opens, hover switches to a neighbour while one is open, Alt or F10 focuses the bar, Alt+letter mnemonics (`&File` marks F), Left/Right move between menus.
- [ ] **Context menu:** `Surface::ShowContextMenu(menu, position)` opened from a right-click callback (a widget exposes `OnContextMenu`), placed at the pointer.
- [ ] **Accelerators:** `Surface::RegisterAccelerator(KeyChord, Action)` with chords like Ctrl+S, shown in menu items automatically, dispatched after the focus owner declined the key, disabled with their action; conflicts detected at registration.
- [ ] **`ComboBox`:** shows the selected item, opens a list popup (a `ListBox` from Day 194, virtualized), keyboard open/close/select, type-ahead, wheel changes the selection when closed (option), editable variant with a `TextField` (the text and the list stay in sync), selection changed callback.
- [ ] **Tooltips:** any widget's tooltip text shows after a hover delay from the surface clock, hides on press/leave/scroll, positions near the pointer inside the surface; one tooltip at a time.
- [ ] **Lifetime:** a popup whose anchor is destroyed closes; opening a popup while one is opening is serialized; closing during an event handler is deferred (Day 190 rule).
- [ ] **Tests (`tests/ui/popup_test.cpp`, `menu_test.cpp`, `combobox_test.cpp`):** placement and flipping at all four surface edges, outside click closes and is consumed or passed through, nested menu stack closes together, submenu delay, keyboard navigation skipping separators and disabled items, mnemonics and Alt, accelerator dispatch order and disable, context menu position clamped, combobox keyboard and wheel, tooltip delay and hide rules, anchor destroyed closes the popup, no focus left behind (focus returns to the previous owner).
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 202 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 202); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** menus with submenus, a menu bar with mnemonics, context menus, comboboxes, accelerators and tooltips work by mouse and keyboard and overlay anything without breaking focus or routing.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
