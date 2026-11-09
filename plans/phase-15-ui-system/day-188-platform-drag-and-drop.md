# Phase 15 / Day 188 — Platform: OS Drag and Drop (Wayland, X11)

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 185, 187.

Dropping files and text from other programs onto a window, and dragging text and files out of one. The in-toolkit drag machinery is Day 200; this day is the platform part. Win32 follows on Day 205.

## Tasks

- [ ] **API on `os::Window`:** `SupportsDragAndDrop()`; `SetDropAccept(formats)` (what the window wants: `Text`, `FileList`, `Custom(name)`, or nothing to refuse drags), events `DragEnter{position, formats, allowed_actions}`, `DragOver{position}` (the window answers with the action it would take: copy, move, link, none; this feeds the cursor), `DragLeave`, `Drop{position, action, format availability}` with the data read through `RequestDropData(format)` (asynchronous like the clipboard, the answer is a `ClipboardData`-style `DropData` event); `BeginDrag(DragSource&)` with the offered formats, bytes or a lazy callback, allowed actions and an optional drag image (RGBA, hotspot), ending in `DragEnded{action}` (dropped with an action, or cancelled).
- [ ] **Wayland (receive):** `wl_data_device` `enter/motion/leave/drop`, `wl_data_offer.accept` and `set_actions` to choose the action, `receive` after the drop, `finish` when read; (**send**) `wl_data_device.start_drag` with a `wl_data_source`, the drag icon surface from the drag image, `dnd_drop_performed`, `dnd_finished`, `cancelled`, `action` events; the same non-blocking pipe transfer as the clipboard (Day 187).
- [ ] **X11 (XDND, version 5):** `XdndAware` property on the window; receive `XdndEnter` (types, including the `XdndTypeList` case for more than three types), `XdndPosition` (answer `XdndStatus` with the action and the accept rectangle), `XdndLeave`, `XdndDrop` (convert the selection `XdndSelection`, answer `XdndFinished`); send: grab the pointer, find the target window under the pointer (walking children for `XdndAware`, honouring `XdndProxy`), send `XdndEnter/Position/Leave/Drop`, serve `XdndSelection`; a drop on a window that does not answer in 1 s cancels; the drag image drawn as a small override-redirect ARGB window following the pointer where the server has a compositor, else omitted (documented).
- [ ] **Actions and feedback:** the window reports the action it would take from `DragOver`; the platform shows it (the compositor's cursor on Wayland, `XdndStatus` on X11); modifiers (Ctrl copy, Shift move) are read from the event stream; Escape cancels an outgoing drag.
- [ ] **Safety:** the same data rules as the clipboard (UTF-8 validation, length caps, `file://` parsing, no network URIs); a drag that enters with a huge file list is truncated at 10,000 entries with a counter; an offer that changes mid-drag is re-read.
- [ ] **Tests (fake-protocol, then real servers):** a scripted peer drags text, a file list and a custom format onto the window and the event order, positions, action negotiation, data bytes and `Drop` timing are checked; leave and cancel; a drop while the previous drop's data is still being read; `BeginDrag` offering text and files to a scripted peer with each action, cancel by Escape, and a peer that never answers; X11 between two connections of the same process; hostile input (an offer with 1,000 types, an invalid URI list); no leak of surfaces, windows, fds or atoms (counted).
- [ ] **Probe program:** `game/input_probe` prints drag events and drops, and starts a drag of a fixed string and a fixed file path; tried by eye against a file manager and a text editor on both backends, recorded in this file.
- [ ] **BACKEND checklist:** `engine/platform/BACKEND.md` lists the drag-and-drop operations and events; Win32 (OLE `IDropTarget`/`IDropSource`, `DoDragDrop`, `CF_HDROP`) and macOS (`NSDraggingDestination`) say "not implemented yet" and name Day 205 / Phase 22.
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 202 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 202); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a window can accept text, file and custom drops from other programs and start drags to them on Wayland and X11, with action negotiation and cancel, tested against scripted peers and by eye with real programs.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
