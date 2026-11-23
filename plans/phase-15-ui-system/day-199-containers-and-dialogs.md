# Phase 15 / Day 199 — Tab View, Splitter, Dialog Frame and Modal Dialogs

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 193, 194, 198.

The container widgets that need input, and modality.

## Tasks

- [x] **`TabView`:** tab headers (text, optional icon, optional close button), a page container showing the active page, keyboard (Ctrl+Tab, arrow keys on the header), tab overflow with scroll arrows or a drop-down list, selected tab changed callback; pages stay alive when hidden (state kept) or are created lazily by a factory.
- [x] **`Splitter`:** two or more panes, horizontal or vertical, draggable handles with capture and the resize cursor, min sizes per pane honored, proportional resize when the container resizes, double-click resets, collapsible pane option; layout state serializable as plain numbers (for a later editor layout file).
- [x] **`Dialog` and `WindowFrame`:** a framed panel with a title bar, optional close button, content, and a button row; movable inside the surface by dragging the title bar (clamped to the surface); resizable by edges when enabled (resize cursors); stays inside its parent layer.
- [x] **Modality:** `Surface::ShowModal(dialog)` blocks input to everything beneath (dim layer drawn from the theme), keeps Tab focus inside, Escape cancels, Enter fires the default button, modal stack for dialogs over dialogs, `Close(result)` returns focus to the previous owner; popups opened inside the dialog work; a modal over a closed-by-the-app anchor does not crash.
- [x] **Common dialogs built from the widgets:** `MessageBox` (title, text, buttons OK/Cancel/Yes/No) and `ConfirmDialog` helpers as examples and for the settings screen's restart prompt; no file dialogs this phase (they need the file layer's listing UI and are an editor concern: Phase 30).
- [x] **Docking is not implemented** (stretch in the phase file): documented as a non-goal; the splitter + tab view are the building blocks Phase 30 uses.
- [x] **Tests (`tests/ui/tab_splitter_test.cpp`, `dialog_test.cpp`):** tab selection by mouse and keys, overflow, lazy page factory called once; splitter drag results, min sizes, proportional resize, double-click reset, collapse; dialog drag clamped, edge resize with min size; modal blocks hover and clicks beneath (counted), Tab cycles inside, nested modals, Escape/Enter, focus restored, dim layer drawn; message box results.
- [x] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 205 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 205); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [x] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [x] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** tab views, splitters, framed dialogs and modal dialogs work, with modality blocking everything beneath and focus returning where it was.

## Notes

- `TabView` hides unselected pages with `SetCollapsed` (state kept); lazy pages come from `AddLazyTab`'s factory, called once.
- `Splitter` stores sizes as weights and exposes `Fractions`/`SetFractions` for a layout file; collapse happens at half the minimum.
- Modality is a fourth `Surface` layer (`modals_`) between the root and the popups; each modal has a `ModalDim` shade and a saved focus owner.
- Limitation: Enter inside a focused text field belongs to the field, so it does not press the default button.
- **Runs (Linux):** plain `ran 1793 of 4568 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`; asan `ran 1295 of 4568 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`; tsan `ran 986 of 4568 tests (phases: all; flavors: tsan; display: included; crash tests: excluded)`. `ctest -L docs` passes; the seven gates pass; Clang and GCC, unity on and off, build clean.

## Post-implementation code review

`/code-review high`: 2 findings, 1 fixed, 1 declined.

- **Fixed:** the test "a second close does nothing" used the dialog after the first `Close` destroyed it (asan and tsan reported heap-use-after-free; plain passed by luck). The test now closes again from inside the `OnClosed` listener.
- **Declined:** Enter in a focused text field does not press the dialog's default button. The field's own Enter handling (`OnSubmit`) takes precedence by design; documented in the guide.
