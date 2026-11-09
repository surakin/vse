# Phase 15 / Day 205 — Windows PC: Win32 Input Events, Pointer Lock, Cursor, DXIL, Everything Built and Run

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 185-204 (done on the Linux machine); the Windows PC (CLAUDE.md, "Windows is a supported platform").

The day the Windows machine catches up: Phase 14's open Windows items and all of Phase 15's shared code.

## Tasks

- [x] **Build and run first** (done early, after Day 198, at the user's request; see Notes): `cmake --preset windows-msvc` and `windows-clang-cl`, both unity modes; fix what the Windows compilers report in `engine/text`, `engine/ui`, `renderer` and the demos (warnings are errors); `scripts/test-phase.sh --affected` plain and asan; record the lines. Everything this phase added is checked here before anything else is started.
- [x] **Phase 14 leftovers (KNOWN-ISSUES):** run the `shaders` target on Windows to mint `text.vert/frag.dxil.inc` and the new `ui.vert/frag.dxil.inc`; include them in `text_renderer.cpp` and `ui_renderer.cpp` (the `kTextVertShader` style sets with both blobs); add them to `tests/renderer/shader_dxil_test.cpp`; drop the Vulkan-only guards in the text and UI GPU tests; FreeType's Windows build (`third_party/freetype/CMakeLists.txt`: the quoted `FT_CONFIG_OPTIONS_H`, `/W0`, the module list) verified; the text demo overlay run on DX12 and Vulkan.
- [x] **Win32 event stream (`win32_window.cpp`):** `WM_KEYDOWN/KEYUP/SYSKEYDOWN` -> `KeyDown/KeyUp` with the repeat bit from `lParam`; **text** from `WM_CHAR` (UTF-16 surrogate pairs combined into one UTF-8 scalar; `WM_UNICHAR` ignored; dead keys handled by the system); `WM_MOUSEMOVE` (with `TrackMouseEvent` for leave), buttons, `WM_MOUSEWHEEL/HWHEEL` scaled to notches (the Linux backends' 10-units-per-notch convention in `input.h` is kept for `InputState`), focus from `WM_SETFOCUS/KILLFOCUS`, resize from `WM_SIZE`; the queue and overflow counter from Day 185; the modal-loop callback (`SetModalLoopCallback`) keeps events queued while the user drags the frame.
- [x] **Win32 clipboard and drag and drop:** `OpenClipboard`/`SetClipboardData` with `CF_UNICODETEXT` (UTF-16 <-> UTF-8, CRLF kept as the system has it) and `CF_HDROP` file lists, a custom format through `RegisterClipboardFormat`, change notification by `WM_CLIPBOARDUPDATE` (`AddClipboardFormatListener`), the same asynchronous request/timeout interface as Day 187 (the Win32 calls are synchronous, the answer is queued as an event); OLE drag and drop: `OleInitialize` on the window thread, a `IDropTarget` mapping to `DragEnter/Over/Leave/Drop`, `DoDragDrop` with an `IDropSource` and an `IDataObject` for outgoing drags (text, `CF_HDROP`, custom), drag image through `IDragSourceHelper`; `SupportsClipboard`/`SupportsDragAndDrop` true, primary selection unsupported; the Day 187/188 fake-peer tests run unchanged and real-window tests exchange data with a second window in the test process.
- [x] **Win32 cursor and pointer lock:** `SetCursor` with `LoadCursor` standard cursors on `WM_SETCURSOR`; custom cursor images through `CreateIconIndirect` from a 32-bit DIB section (premultiplied RGBA converted, hotspot set, destroyed on replace and on window destroy, the size limit of Day 186); `SupportsCursorImage` true; lock by `ClipCursor` to the client rect + hide the cursor + raw input (`RegisterRawInputDevices`, `WM_INPUT` relative motion); released on `WM_KILLFOCUS`, window destroy and process exit; `SupportsCursorShape`/`SupportsPointerLock` true.
- [x] **Windows tests:** the fake-backend event tests of Day 185 run unchanged; real-window tests post synthetic messages (`SendInput` where permitted, else `PostMessage` into the window procedure) and check order, text (BMP, surrogate pair, dead key through a layout loaded for the test where possible), wheel scaling, leave, focus; lock/clip state machine; `BACKEND.md` Windows sections filled in and `window_backend_lists` green.
- [x] **`ui_demo` on Windows:** scripted run under Direct3D 12 and Vulkan, 1000 frames with validation (GPU-based validation on D3D12), both window sizes, `--scale 2` on a HiDPI setting if the PC has one; goldens compared on the Windows GPU (a Windows golden set or a tolerance note per `KNOWN-ISSUES.md`, as the Phase 13 goldens did).
- [ ] **By-eye on Windows** (not done: needs a person at the PC; listed in `KNOWN-ISSUES.md`):** mouse look in the viewport, cursor shapes, menus near edges, text field typing with AltGr characters, resizing by the frame while the UI keeps drawing (the modal-loop callback), recorded in this file.
- [x] **Close the Windows entries in `KNOWN-ISSUES.md`:** the Phase 14 Day 175 and Day 180 and Day 182 "open until the Windows PC runs it" items are removed in this commit if they pass, or rewritten with what failed.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [x] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [x] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [x] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the Windows PC builds and runs everything of Phases 14 and 15 with both compilers and both graphics backends, the text and UI shaders have DXIL, and the Win32 backend produces the same events as the Linux ones.

## Notes

- **Build and run first, done early** (after Day 198 was pushed from the Linux machine, at the user's request; Days 199-204 had not been done yet,
  so their code is not in this check). The four Windows trees (`build-msvc`, `build-clang-cl`, `build-windows-msvc-nounity`,
  `build-windows-clang-cl-nounity`) build clean after four test fixes, all from MSVC: `tests/text/cursor_test.cpp` and `tests/ui/draw_list_test.cpp`
  made infinity by multiplying float constants (C4056, an error under `/WX`: now `std::numeric_limits<f32>::infinity()` and `quiet_NaN()`);
  `tests/ui/scroll_test.cpp`'s allocation counter used `std::aligned_alloc`, which the UCRT lacks (now `_aligned_malloc` with `_aligned_free` on
  Windows, as `tests/renderer/render_graph_test.cpp` does); `scroll_test.cpp` and `popup_test.cpp` passed `int` sizes to `f32` constructors through
  `MakeUnique` (C4244: now float literals). clang-cl found nothing more. One build flake: `LNK1163` on a damaged object in the MSVC non-unity
  tree, gone after rebuilding the object (filed).
- **Runs on Windows:** `scripts/test-phase.sh --affected`: `ran 1539 of 3000 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 413 s`, one failure, `shaders_check`: the text and UI shaders had no
  DXIL. `--affected --flavor asan`: `ran 1196 of 3000 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 151 s`, all passed. The gates (`module_deps_check`, `platform_neutral_check`,
  `ui_widget_style_check`, `window_backend_lists`) pass on Windows. The text overlay, its golden, `renderer_demo_overlay_win32`,
  `renderer_demo_screenshot_text` and the UI text pass ran and passed there with Vulkan.
- **DXIL minted:** the `shaders` target on Windows wrote `text.vert/frag.dxil.inc` and `ui.vert/frag.dxil.inc` (slangc v2026.18.2, dxcompiler
  1.9.2602.17) and rewrote nothing else (every other minted output is byte-identical); `shaders_check` and `shaders_portability` pass. **Not done
  yet** (the rest of the Phase 14 leftovers task): `text_renderer.cpp` and `ui_renderer.cpp` do not use the new DXIL, so the text and UI paths still
  fail on Direct3D 12 and their GPU tests run Vulkan only.

- **The rest of the day** (commits "part 1" to "part 5" and the closing commit):
  - **DXIL in use:** `text_renderer.cpp` and `ui_renderer.cpp` include `text.vert/frag.dxil.inc` and `ui.vert/frag.dxil.inc` and use the
    `kTextVertShader`-style sets with both blobs; `tests/renderer/shader_dxil_test.cpp` has the four new rows; the 27 Vulkan-only guards of the text and UI
    GPU tests are gone, so they run on Direct3D 12 too (goldens included, on the Radeon 860M); `engine/ui`'s Windows branch links `foundation_settings`.
    The `renderer_demo` text overlay runs clean on DX12 and Vulkan with validation. The Phase 14 Day 180 and Day 182 and Phase 15 Day 190 entries of
    `KNOWN-ISSUES.md` are closed (what remains of them is rewritten).
  - **Event stream** (`win32_window.cpp`): keys with repeat and the modifiers held before the key, the extra `KeyUp` when one Shift release ends both,
    `WM_CHAR` text (surrogate pairs joined, control characters and Ctrl or Alt alone give none; Ctrl and Alt together, Windows' AltGr, do), pointer enter
    (the first move inside), move, buttons (a repeated press of a held button gives nothing), leave (also when a captured drag leaves), the wheel in notches,
    focus, `Resize` (the stream starts with the settled size), `KeyUp` for every held key before `FocusLost`. A window opened without an `InputState`
    keeps its own, so the events always have key and modifier tracking.
  - **Cursor and lock:** the system cursors on `WM_SETCURSOR` over the client area; images through `CreateIconIndirect` from a 32-bit DIB (premultiplied
    to straight alpha, at pixel size, not remade for an equal image, the old one destroyed after the new one shows); the lock is `ClipCursor` to the
    client centre (kept there on move and resize), the cursor hidden and raw mouse input for `RelativeMotion`; released on unlock, focus loss and close.
  - **Clipboard** (`win32_clipboard.cpp`): `CF_UNICODETEXT`, `CF_HDROP` (native paths through `os::win32::AcceptLocalPath`: drive-absolute only) and
    registered custom formats; requests answered at once and announced by `ClipboardData`; `WM_CLIPBOARDUPDATE` gives `ClipboardChanged` for another
    program's change (the window's own is skipped by the sequence number); opening retries 20 times 5 ms apart.
  - **Drag and drop** (`win32_dragdrop.cpp`): `IDropTarget` to `DragEnter/Over/Leave/Drop`, every accepted format read during `Drop` (OLE's data object
    is gone afterwards); `BeginDrag` runs `DoDragDrop` (modal) with an `IDataObject` and `IDropSource`, refuses without a mouse button down, and calls
    the new `os::DragSource::started` first. The drag image is not drawn (`IDragSourceHelper` not used; filed).
  - **Tests:** `tests/windows/win32_events_windows_test.cpp`, `win32_clipboard_windows_test.cpp`, `win32_dragdrop_windows_test.cpp` (phase 15; the
    `tests/windows/` line of `tests/phases.txt` split per file) post messages to the window and drive the drop target with a data object directly; no
    real input was injected (`SendInput` would type into whatever has the focus), and no keyboard layout was loaded (a system setting), so a dead key is
    not tested. `windows_platform_tests`: 56 test cases, 846 assertions.
  - **`ui_demo`:** `ui_demo_script_win32` (Vulkan) and the new `ui_demo_script_win32_dx12` pass, 18 of 18 `ui_demo` ctests; 1000 frames with `--validation`
    on DX12 (GPU-based validation) and Vulkan, at the display's scale and at `--scale 1`: exit 0, no validation error or warning. The six `ui_demo`
    goldens pass on the Radeon 860M with both backends.
  - **DPI (found by the user):** `ui_demo` in a window drew at scale 1 on the 200% display, so every widget was half size. New
    `os::Window::ContentScale()` (Win32: the DPI over 96, following `WM_DPICHANGED`; Wayland and X11: 1) and `ui::WindowAdapter::SetFollowWindowScale`
    (the surface follows the window's scale, a monitor change included); `ui_demo` uses it unless `--scale` is given (`--screenshot` stays at 1, so the
    goldens are unchanged). The user confirmed it looks right.
  - **Builds:** `build-msvc`, `build-clang-cl`, `build-windows-msvc-nounity` and `build-windows-clang-cl-nounity` build clean (warnings are errors).
- **Runs on Windows:** `scripts/test-phase.sh --affected`: `ran 1746 of 3398 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 306 s, all passed`. `--affected --flavor asan`: `ran 1385 of 3398 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 141 s, all passed`. `ctest -L docs`: 7 of 7. The first plain run of the
  closing state also failed `platform_neutral_check` (`win32_dragdrop.cpp` included `shlobj.h` without the `win32.h` wrapper first: fixed) and
  `ui_demo_settings_help` (the help golden: updated), and two flaky tests (filed): `The demo frame's text overlay survives a device reset, a pipeline
  rebuild and a lost device` once on DX12 (passed 3 of 3 alone) and `Throughput scales with participant count`. The Day 159 mouse test failed 2 of 3
  runs of `windows_platform_tests` by itself (it reads the last mouse message, so a real mouse move changes it; filed).
- **Docs:** `os::Window::ContentScale`, `ui::WindowAdapter::SetFollowWindowScale` and `os::DragSource::started` documented; `win32_dragdrop.h` excluded
  from the reference as backend-internal (as `x11_dnd.h` is); `docs/reference/` regenerated; `engine/platform/BACKEND.md` (the `windows` section),
  `docs/guide/windows.md` ("Input, the cursor, the clipboard and drag and drop") and `docs/guide/ui.md` ("On Windows") written; `game/ui_demo/README.md`.
- **Shared code changed here** (CMake, the phase map, the adapter, the demo, tests run on Linux): open in `KNOWN-ISSUES.md` until the Linux machine builds
  and tests it.

## Post-implementation code review

`/code-review high` over the Day 205 changes (from the commit before part 1 to the working tree): 7 findings, 6 fixed, 1 declined.

1. **Fixed:** the adapter handed a drag to the OS only after `BeginDrag` returned, and Win32's `BeginDrag` is modal, so for the whole drag the surface kept
   running it as its own (drag visual, hover targets, a drop back onto itself) while the modal-loop callback drew. New `os::DragSource::started`, called by
   Win32 before `DoDragDrop`; the adapter hands off there (and still after the return, which does nothing a second time). The Linux contract that a
   refusing window keeps the drag here is unchanged. Test: `a modal OS drag (Win32) is handed off when it starts, not when BeginDrag returns`
   (`tests/ui/clipboard_test.cpp`, a scripted window that runs the drag inside `BeginDrag`); it fails without the fix.
2. **Fixed:** text typed with Ctrl+Alt (Windows' AltGr: Ctrl+Alt+Q is '@' on a German layout) was dropped as a shortcut. Only one of the two held is a
   shortcut now. Test: `Windows events: Ctrl and Alt together are Windows' AltGr and type text; Alt alone types none`; it fails without the fix.
3. **Fixed:** `Win32DataObject::Set` copied the `CF_HDROP` block through an unchecked `GlobalLock`. A failed lock now refuses the offers. No test: the
   lock of a fresh block cannot be made to fail.
4. **Fixed:** `BeginDrag` set the modal timer with a literal `1`; the window procedure only runs the callback for its own constant. Both use
   `kWin32ModalTimerId` (`win32_dragdrop.h`) now. No behaviour change, no test.
5. **Fixed:** `Drop` answered with the allowed effects of the last `DragOver`, not the ones OLE passes to `Drop`. Test: `Windows drag and drop: the drop
   is answered with the effects the source allows at the drop`; it fails without the fix.
6. **Fixed:** `Win32Window::ContentScale` had no test. Test: `Windows window: the content scale is the DPI over 96 and follows a DPI change`.
7. **Declined:** the OLE objects count references but free nothing and die with the window, so a drop target in another program that kept the data object
   after `DoDragDrop` returned would call into freed memory after the window closed. Self-deleting COM objects need a raw `new`, which `raw_new_check`
   forbids; a reference left at close is logged and none was seen. Filed in `KNOWN-ISSUES.md`.

**Found by the user after the day's commit** (testing `ui_demo` in a window): every click opened the File menu. The Win32 `PointerButton` events carried
no position, so the adapter placed every click at (0, 0), where the File menu is; the Linux backends always set it. Fixed in `MouseButton`;
`input_event.h` now says that `PointerButton` carries `x` and `y`; the event-stream test checks the press and release positions (it fails without the
fix). `windows_platform_tests` and `ui_tests` pass apart from the filed Day 159 mouse test; docs 7 of 7; four trees clean.
