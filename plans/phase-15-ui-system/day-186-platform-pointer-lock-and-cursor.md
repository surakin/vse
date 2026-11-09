# Phase 15 / Day 186 — Platform: Relative Pointer, Pointer Lock and Cursor Shape (Wayland and X11)

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Day 185.

What the viewport widget needs for camera control (hide the cursor and read relative motion) and what every widget needs to look right (resize arrows, a text I-beam).

## Tasks

- [ ] **API on `os::Window`** (defaults do nothing and report false, so Win32 and macOS compile): `SetCursor(CursorShape)` with `CursorShape` = Arrow, IBeam, Hand, ResizeHorizontal, ResizeVertical, ResizeDiagonalNWSE, ResizeDiagonalNESW, ResizeAll, NotAllowed, Hidden; `SupportsCursorShape()`; `SetCursorImage(const CursorImage&)` / `ClearCursorImage()` / `SupportsCursorImage()` (a custom cursor from RGBA8 premultiplied pixels, a size up to 128x128 and a hotspot, copied by the window; it replaces the shape until cleared; a scale factor field picks the right size on HiDPI); `SetPointerLock(bool)` / `PointerLocked()` / `SupportsPointerLock()`; relative motion arrives as a new event `RelativeMotion{dx, dy}` (device units, unaccelerated where the server gives them) and only while locked.
- [ ] **Wayland cursor shape:** bind `wp_cursor_shape_manager_v1` when the compositor offers it (generated bindings committed under `linux/wayland/generated/` with `VENDORED.md` updated, same pattern as xdg-shell); when it does not, load the cursor theme through `libwayland-cursor` only if that is already allowed by the plan's library line (it ships with the reference Wayland client library) - otherwise report `SupportsCursorShape() == false` and keep the default cursor; decide at implementation which, and file the other as the fallback in `KNOWN-ISSUES.md`.
- [ ] **Custom cursor images:** Wayland through `wl_pointer.set_cursor` with a `wl_shm` buffer on a cursor surface (buffer scale from the window's scale; the image is re-set on every `wl_pointer.enter`, since the serial changes), X11 through `XcursorImageCreate`/`XcursorImageLoadCursor` (libXcursor, check against the plan's library line and `BUILD.md`; the fallback is `XRenderCreateCursor` or a 1-bit `XCreatePixmapCursor`, recorded if used); both cache by image hash and free on replace and on window destroy. The image is data, so a game can load it from its own assets; no image decoder is added here.
- [ ] **Wayland pointer lock:** `zwp_pointer_constraints_v1` (lock the pointer, persistent lifetime off) and `zwp_relative_pointer_manager_v1` (relative motion events); generated bindings committed; the lock is released on focus loss and re-requested on focus gain if the caller still wants it; a compositor without the protocols reports `SupportsPointerLock() == false`.
- [ ] **X11 cursor shape and lock:** `XDefineCursor` with the standard font cursors (`XCreateFontCursor`) cached per shape; pointer lock by `XGrabPointer` with a blank cursor, confine to the window, and **XInput2 raw motion** (`XI_RawMotion`, XInput2 is part of libX11's family `libXi`: check it against the plan's library line and file it in `BUILD.md`; if it is not acceptable, use grab + warp-to-center and record the limit); lock released on focus loss and on `Hidden`/`Arrow` restore.
- [ ] **Safety:** a lock never outlives its window or process: destroying the window or a crash handler path releases the grab (X11 does it when the connection closes; the test checks `Destroy` releases it explicitly); a lock request while unfocused is remembered and applied on focus; a second request is a no-op.
- [ ] **Tests:** a custom cursor of 1x1, 32x32 and 128x128 sets, replaces, clears and survives a focus change and a pointer re-enter (fake protocol), is refused above the size limit or with a hotspot outside the image, and does not leak (shm buffer and X cursor freed, counted); fake-protocol tests for the request/release state machine (focus loss, regain, double request, destroy while locked, compositor without the protocol); real-server tests on X11 (grab taken, raw motion arrives from an XTest motion, released on destroy) and, where the session's compositor supports it, Wayland; `SetCursor` on every shape does not fail and is idempotent.
- [ ] **Demo hook:** `rhi_demo` or a new small `game/input_probe` program prints the event stream (Day 185) and toggles lock with a key and cycles cursor shapes with another and sets a generated custom cursor with a third, so a person can see it by eye on both backends; the by-eye result is recorded in this file.
- [ ] **BACKEND checklist:** `engine/platform/BACKEND.md` lists the cursor shape, custom cursor image and lock operations a backend implements; Win32 (`ClipCursor`, `SetCursor`, raw input) and macOS sections say "not implemented yet".
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 205 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 205); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a window can show the standard cursor shapes or a custom cursor image and lock the pointer with relative motion events on X11 and, where the compositor offers the protocols, Wayland, with the fallbacks tested and a person having seen it work.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
