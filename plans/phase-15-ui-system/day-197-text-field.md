# Phase 15 / Day 197 — The Single-Line Text Field

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 185 (text events), 192, 194; Phase 14 Day 179 (`text/cursor.h`).

Editing text with grapheme-correct cursor and selection, the widget that proves Phase 14's cursor API.

## Tasks

- [ ] **`TextField` model:** UTF-8 `String` text, cursor offset and selection anchor (byte offsets on grapheme boundaries only, via `text::NextBoundary`/`PreviousBoundary`), insertion of `Text` events, Backspace/Delete by cluster, Home/End, Left/Right by cluster, Ctrl+Left/Right by word (the `ui` word-boundary function decided on Day 184), Shift extends the selection, Ctrl+A selects all, Enter fires `OnSubmit`, Escape fires `OnCancel`, Tab moves focus (not inserted).
- [ ] **Mouse:** click sets the cursor with `OffsetAtPoint`, drag with capture selects, double-click selects a word, triple-click all, I-beam cursor shape on hover, the click-time threshold and slop from the theme/surface.
- [ ] **Scrolling inside the field:** when the text is wider than the field, the visible window follows the cursor with a margin and clips; selection rectangles from `SelectionRects`; caret drawn from `PositionOfOffset`, blinking from the surface clock (no blinking while the cursor moves, resets on input), blink off when unfocused.
- [ ] **Editing rules:** maximum length (in clusters), a validator callback (accept/reject a proposed text before applying it - used by number fields and the settings screen), read-only mode (cursor and selection work, edits do not), password variant (bullets drawn instead of text, no copy), placeholder text when empty and unfocused.
- [ ] **Undo/redo:** a bounded undo stack of edit operations (insert, delete, replace) with coalescing of consecutive typing; Ctrl+Z / Ctrl+Shift+Z (or Ctrl+Y).
- [ ] **Clipboard:** Ctrl+C/X/V/Insert variants through a `ui::Clipboard` interface; today's implementation is the in-process one (used by offscreen surfaces and tests); the OS-backed implementation (`OsClipboard`, asynchronous paste, primary selection) is wired on Day 200 behind the same interface, so the text field does not change; the interface already has an asynchronous `RequestPaste(callback)` shape so the field is written for it from the start.
- [ ] **Text events and IME:** only `Text` events insert; an unknown key does nothing; documented: no IME or complex-script input (the phase file's scope).
- [ ] **`SpinBox` finished:** embeds a `TextField` with a numeric validator; arrows and wheel adjust; Enter commits; invalid text reverts on focus loss.
- [ ] **Tests (`tests/ui/text_field_test.cpp`):** the Day 184 spike's cases made permanent - a combining mark, a flag, a ZWJ sequence and a Hangul syllable each move and delete as one unit; selection by shift and by mouse; word movement; undo coalescing; max length counts clusters; validator rejection leaves text unchanged; read-only; password draws bullets and refuses copy; caret position pixels equal `PositionOfOffset`; scrolling follows the cursor; blink timing from a fake clock; spin box commit and revert; a randomized edit sequence checked against a simple reference model (seeded).
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 205 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 205); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a text field edits UTF-8 by grapheme cluster with mouse and keyboard, selection, word movement, undo and an in-process clipboard, and the spin box types numbers through it.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
