# Phase 15 / Day 192 — Input Routing: Focus, Capture, Tab Order, the Window Adapter

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 185, 190, 191.

Events from a window (or a test) reach the right widget.

## Tasks

- [ ] **`ui/window_adapter.h`** (the only place `os::Window` appears with `ui`; lives in `game/ui_support` or a small library `ui_window` above `ui` and `platform` so `ui` itself stays window-free): `PumpInto(os::Window&, Surface&)` drains `Window::PollEvent` into `ui::Event`s (pointer coordinates divided by scale into logical units), forwards `Resize` to `Surface::Resize`, maps cursor requests from the surface to `Window::SetCursor`; the library `ui_window` and its edges are added to `ARCHITECTURE.md`.
- [ ] **Pointer routing:** hover is the topmost hit widget (popup layer first, then the tree); enter/leave sent on change, including when the widget under a stationary pointer changes because of layout; **capture**: a press captures the pointer to the pressed widget until release, so a drag outside still reports to it; the release is delivered to the capturer even if the window lost the pointer; `RequestCapture`/`ReleaseCapture` for widgets that start a drag.
- [ ] **Wheel routing:** to the hovered widget, bubbling to the nearest ancestor that consumes it (a scroll view), horizontal wheel to horizontal consumers, never to a widget behind a popup.
- [ ] **Keyboard focus:** one focus owner per surface; click-to-focus for widgets that accept it; `Tab`/`Shift+Tab` order by tree order unless `tab_index` is set, skipping disabled and hidden widgets; focus loss events; focus restore when a popup or modal closes; keys go to the focus owner, bubble to ancestors, then to surface-level shortcuts (accelerators, Day 196).
- [ ] **Text and repeat:** `Text` events go only to a focus owner that accepts text; key repeat events carry the flag; modifiers are the surface's mirror of the last known state, reset on focus loss so a stuck Ctrl cannot persist.
- [ ] **Cursor requests:** a widget under the pointer (or the capturer) names a `CursorShape`; the surface reports the winner to the adapter, which calls `SetCursor` only on change.
- [ ] **Surface-level focus:** the window's focus gain/loss becomes `SurfaceFocused(bool)`: on loss, capture is released and open popups close; an offscreen surface is focused by whoever owns it.
- [ ] **Tests (`tests/ui/input_routing_test.cpp`, with the harness, and `ui_window_test.cpp` with a fake `os::Window`):** hover order with overlapping widgets and popups, enter/leave on layout change, capture through a drag leaving the surface, release delivered, wheel bubbling and horizontal, Tab order with `tab_index`, disabled skipped, shift-Tab wraps, focus restore, text only to text widgets, stuck-modifier reset on focus loss, cursor shape follows hover and capture, adapter divides by scale, resize forwarded.
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 202 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 202); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** events from a window or a test reach the hovered, captured or focused widget in the documented order, Tab navigation works, and the adapter is the only code that knows about the window.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
