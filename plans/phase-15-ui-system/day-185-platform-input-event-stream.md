# Phase 15 / Day 185 — Platform: the Input Event Stream (Wayland and X11)

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Day 184; Phase 1 (`os::Window`, `InputState`, the xkb keyboard).

Today's polled `InputState` cannot say that a key was pressed and released between two polls, has no text, and has no repeat on Wayland. Add the queue every UI needs beside the polled state, on the two Linux backends; the Win32 backend follows on Day 205 behind the same interface.

## Tasks

- [ ] **`platform/input_event.h`** (pure data, in the header-only `platform_input` target): `os::InputEvent` as a tagged value with `type`, `modifiers`, `time_ns` and a union-free payload: `KeyDown`/`KeyUp` (key, `repeat` flag, the XKB keysym or scancode for diagnostics), `Text` (one UTF-8 scalar, up to 4 bytes + length), `PointerMove` (x, y in window pixels), `PointerButton` (button bit, down), `Wheel` (dx, dy in notches, plus the high-resolution value where the protocol gives it), `PointerEnter`, `PointerLeave`, `FocusGained`, `FocusLost`, `Resize` (width, height, serial). Static asserts on size and trivial copyability.
- [ ] **Queue in `os::Window`:** `virtual bool PollEvent(InputEvent* out)` (default false), filled during `PumpEvents`, bounded (1,024 events) with an overflow counter `DroppedEvents()` so a stalled program loses events visibly and never memory; the order of the queue is the order the server delivered; `InputState` keeps being updated exactly as before (all existing tests unchanged).
- [ ] **Wayland producer** (`wayland_input.cpp`): key press/release from `wl_keyboard` with modifiers from `xkb_state`; **text** from `xkb_state_key_get_utf8` on press (not for keys held with Ctrl or Alt, which produce shortcuts, not text); **client-side key repeat**: the compositor sends `wl_keyboard.repeat_info` (rate, delay) and does not repeat, so `PumpEvents` synthesizes `KeyDown{repeat=true}` and the matching `Text` from the monotonic clock while a key is held (stop on release, focus loss, or another key press taking over); wheel from `wl_pointer.axis`/`axis_discrete`/`axis_value120` where bound; enter/leave; focus from `wl_keyboard.enter/leave`; resize from `xdg_toplevel.configure`.
- [ ] **X11 producer** (`x11_window.cpp`): key press/release (X11 repeats as press/release pairs: detect the auto-repeat pair and report one `KeyDown{repeat=true}` and no `KeyUp`, using `XkbSetDetectableAutoRepeat`); **text** from `Xutf8LookupString` (already called, result unused today); wheel from buttons 4-7 (5 and 6 are horizontal); enter/leave from `EnterNotify`/`LeaveNotify`; focus from `FocusIn`/`FocusOut`; resize from `ConfigureNotify`.
- [ ] **Dead-key and compose handling:** a dead key followed by a letter yields one `Text` event with the composed scalar and no `Text` for the dead key itself (XKB compose on both backends through `xkb_compose_*` if the library is present, else the lookup string); documented limit: no IME (the phase file's scope).
- [ ] **Fake-backend tests** (`tests/foundation/window_events_test.cpp` or beside the existing keyboard tests): drive the xkb layer with scripted key sequences (layouts `us`, `de`, `fr` as the existing `keyboard_input_test.cpp` does) and check the event order, `Text` contents (including `é` through a dead key, `ß`, Euro sign with AltGr), the Ctrl+key case (no text), repeat timing with a fake clock (first repeat after the delay, then the rate), release stops repeat, focus loss stops repeat and releases keys.
- [ ] **Real-server tests** (skip with a printed reason without a display, like `x11_window_test.cpp` and `wayland_window_test.cpp`): on Xvfb/Xwayland or the session's server, send synthetic key and pointer events (XTest on X11; `wtype`/`ydotool` are not assumed, so the Wayland test drives the protocol objects through the test compositor seam the existing Wayland tests use) and check the events arrive in order and `InputState` matches.
- [ ] **Queue contract tests:** overflow counts and drops the newest, never the oldest; `PollEvent` after `PumpEvents` returns events in order exactly once; a closed or invalid window returns false; the polled `InputState` after a burst equals the state a replay of the events gives.
- [ ] **BACKEND checklist:** the list of events a window backend must produce goes into `engine/platform/BACKEND.md` (new, same pattern as `engine/foundation/io/BACKEND.md`), with a gate `window_backend_lists` in the style of `io_backend_lists` that fails when a platform directory (`linux`, `windows`, `macos`) lacks a section; the Windows and macOS sections say "not implemented yet" and name Day 205 / Phase 22.
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 205 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 205); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a program can read press, release, repeat, text, wheel, enter/leave, focus and resize events in order from `Window::PollEvent` on Wayland and X11, tested with scripted layouts and the real servers, while `InputState` behaves as before.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
