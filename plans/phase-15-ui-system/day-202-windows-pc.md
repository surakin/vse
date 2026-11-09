# Phase 15 / Day 202 — Windows PC: Win32 Input Events, Pointer Lock, Cursor, DXIL, Everything Built and Run

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 185-201 (done on the Linux machine); the Windows PC (CLAUDE.md, "Windows is a supported platform").

The day the Windows machine catches up: Phase 14's open Windows items and all of Phase 15's shared code.

## Tasks

- [ ] **Build and run first:** `cmake --preset windows-msvc` and `windows-clang-cl`, both unity modes; fix what the Windows compilers report in `engine/text`, `engine/ui`, `renderer` and the demos (warnings are errors); `scripts/test-phase.sh --affected` plain and asan; record the lines. Everything this phase added is checked here before anything else is started.
- [ ] **Phase 14 leftovers (KNOWN-ISSUES):** run the `shaders` target on Windows to mint `text.vert/frag.dxil.inc` and the new `ui.vert/frag.dxil.inc`; include them in `text_renderer.cpp` and `ui_renderer.cpp` (the `kTextVertShader` style sets with both blobs); add them to `tests/renderer/shader_dxil_test.cpp`; drop the Vulkan-only guards in the text and UI GPU tests; FreeType's Windows build (`third_party/freetype/CMakeLists.txt`: the quoted `FT_CONFIG_OPTIONS_H`, `/W0`, the module list) verified; the text demo overlay run on DX12 and Vulkan.
- [ ] **Win32 event stream (`win32_window.cpp`):** `WM_KEYDOWN/KEYUP/SYSKEYDOWN` -> `KeyDown/KeyUp` with the repeat bit from `lParam`; **text** from `WM_CHAR` (UTF-16 surrogate pairs combined into one UTF-8 scalar; `WM_UNICHAR` ignored; dead keys handled by the system); `WM_MOUSEMOVE` (with `TrackMouseEvent` for leave), buttons, `WM_MOUSEWHEEL/HWHEEL` scaled to notches (the Linux backends' 10-units-per-notch convention in `input.h` is kept for `InputState`), focus from `WM_SETFOCUS/KILLFOCUS`, resize from `WM_SIZE`; the queue and overflow counter from Day 185; the modal-loop callback (`SetModalLoopCallback`) keeps events queued while the user drags the frame.
- [ ] **Win32 cursor and pointer lock:** `SetCursor` with `LoadCursor` standard cursors on `WM_SETCURSOR`; custom cursor images through `CreateIconIndirect` from a 32-bit DIB section (premultiplied RGBA converted, hotspot set, destroyed on replace and on window destroy, the size limit of Day 186); `SupportsCursorImage` true; lock by `ClipCursor` to the client rect + hide the cursor + raw input (`RegisterRawInputDevices`, `WM_INPUT` relative motion); released on `WM_KILLFOCUS`, window destroy and process exit; `SupportsCursorShape`/`SupportsPointerLock` true.
- [ ] **Windows tests:** the fake-backend event tests of Day 185 run unchanged; real-window tests post synthetic messages (`SendInput` where permitted, else `PostMessage` into the window procedure) and check order, text (BMP, surrogate pair, dead key through a layout loaded for the test where possible), wheel scaling, leave, focus; lock/clip state machine; `BACKEND.md` Windows sections filled in and `window_backend_lists` green.
- [ ] **`ui_demo` on Windows:** scripted run under Direct3D 12 and Vulkan, 1000 frames with validation (GPU-based validation on D3D12), both window sizes, `--scale 2` on a HiDPI setting if the PC has one; goldens compared on the Windows GPU (a Windows golden set or a tolerance note per `KNOWN-ISSUES.md`, as the Phase 13 goldens did).
- [ ] **By-eye on Windows:** mouse look in the viewport, cursor shapes, menus near edges, text field typing with AltGr characters, resizing by the frame while the UI keeps drawing (the modal-loop callback), recorded in this file.
- [ ] **Close the Windows entries in `KNOWN-ISSUES.md`:** the Phase 14 Day 175 and Day 180 and Day 182 "open until the Windows PC runs it" items are removed in this commit if they pass, or rewritten with what failed.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the Windows PC builds and runs everything of Phases 14 and 15 with both compilers and both graphics backends, the text and UI shaders have DXIL, and the Win32 backend produces the same events as the Linux ones.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
