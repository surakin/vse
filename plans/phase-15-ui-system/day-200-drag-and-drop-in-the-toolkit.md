# Phase 15 / Day 200 — Drag and Drop in the Toolkit

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 188, 192, 194 (input routing), 196 (list box), 197 (text field), 199 (containers).

Widgets that start drags, widgets that accept drops, drag visuals, and the bridge to the OS drag and drop of Day 188. The same machinery serves a drag inside one surface and a drag from or to another program.

## Tasks

- [ ] **Model (`ui/drag_drop.h`):** `DragPayload` = a list of typed entries (`Text`, `FileList`, `Custom(name, bytes)`, and an application pointer entry valid only inside the process), allowed actions (copy, move, link), an optional drag visual (an `ImageId` or a widget tree snapshot built by the source); `DragSource` (a widget calls `Surface::BeginDrag(payload, visual)` from a press-and-move after a slop distance from the theme); `DropTarget` interface on widgets: `CanAccept(payload, position) -> action`, `OnDragEnter/Over/Leave`, `OnDrop(payload, position, action)`.
- [ ] **Routing:** a drag takes pointer capture on the surface; hover during a drag goes to drop targets only (the topmost widget under the pointer that accepts, else its accepting ancestor); popups and the top layer receive drags like any widget; auto-scroll when the pointer rests near the edge of a `ScrollView` or `ListBox` (speed from the distance, from the surface clock); a hover-delay hook (tabs and submenus open while a drag waits over them); Escape cancels; release over no target cancels with the cancel callback.
- [ ] **Visuals:** the drag visual draws in the top layer following the pointer with an offset; the drop target highlights from the theme (variant `drop-target`, state `drag-over`); the cursor shows the action (copy/move/not-allowed) through `CursorShape`; an insertion line for list reordering from the theme.
- [ ] **OS bridge (`ui_window`):** a drag that leaves the surface becomes an OS drag (`Window::BeginDrag` with the payload's text, file and custom entries) when the payload has any OS-representable entry; an OS `DragEnter/Over/Drop` becomes a `DragPayload` over the surface's drop targets, with the data requested asynchronously and the drop delivered when it arrives (the target sees `OnDrop` only with complete data; a failed read reports a failure message, not a half drop); a pointer-only payload is never offered to the OS.
- [ ] **Built-in uses:** `ListBox` item reordering and drag-out of selected items (opt-in, a model hook `MoveItems`); `TextField` accepts a text drop (inserts at the drop position, moves the selection when dragging from itself with the move action) and offers its selection as a drag source; `TabView` tab reordering and drag-to-hover-select; `Window` file drop to the surface as a drop target for `FileList` with an `OnFilesDropped` callback on the root; `Splitter` and dialogs are unaffected.
- [ ] **Clipboard in the toolkit:** `ui::Clipboard` (Day 197's in-process interface) now has the OS implementation behind it - `OsClipboard` over `Window::SetClipboardText/RequestClipboardText` from Day 187, asynchronous: paste in a text field requests, shows nothing until the data event arrives, then inserts (and undo treats it as one edit); Paste is enabled from `ClipboardChanged` and `ClipboardHasFormat`; the in-process clipboard stays as the implementation for offscreen surfaces and tests; primary selection: select-to-copy and middle-click paste in text fields where `Primary` is supported (Linux), off elsewhere.
- [ ] **Menus and accelerators:** Edit > Cut/Copy/Paste/Select All items enable from the focus owner (`CanCut/CanCopy/CanPaste`), Ctrl+C/X/V and Shift+Insert/Delete accelerators route to the focus owner before surface shortcuts.
- [ ] **Tests (`tests/ui/drag_drop_test.cpp`, `clipboard_test.cpp`, with the harness and a fake `os::Window`):** begin/slop/cancel rules; drop target selection (accepting ancestor, overlapping widgets, popups); auto-scroll rates with a fake clock; list reorder results (including drop at the ends and on itself); text drop positions and move-vs-copy within one field; tab reorder; OS drag in (text, files, custom, a failed read) and out (payload entries mapped to formats, pointer entries withheld); action to cursor mapping; async paste: nothing inserted before the data event, one undo step after, a timeout shows the failure; Paste enable/disable follows `ClipboardChanged`; primary selection copy and middle-click paste; hostile payloads (huge text, invalid UTF-8) capped without crashing.
- [ ] **`ui_demo` hooks (finished on Day 204):** the list box in the left panel reorders by dragging, the text field accepts dropped files (shows the paths) and text, and Edit > Copy/Paste works against other programs.
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 202 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 202); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** widgets start and accept drags inside a surface, drags cross to and from other programs, and text fields copy, cut and paste through the OS clipboard asynchronously with the in-process clipboard kept for offscreen surfaces.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
