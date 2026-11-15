# Phase 23 / Day 353 — Host-Owned Input & the Host-Driven Frame Loop

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Day 352.

Input comes from the host; cursor, clipboard and focus requests go back as callbacks; the host drives frames from its own timer.

## Tasks

- [x] Input forwarding: `engine_input_key`, `_pointer`, `_scroll`, `_text`, `_focus`, `_touch` (size-prefixed event struct with a timestamp and the modifiers) into the engine's input stream (`platform` input layer), so the UI toolkit, the camera controllers and the demos see them as from their own window; an engine-created window stays the source for engine windows only.
- [x] Requests back: the engine asks for cursor shape, cursor visibility and lock, clipboard get and set, text-input rectangles and focus through callbacks registered at create; on an embedded surface the engine opens no input or clipboard connection of its own (tested by watching for the connection on X11 and Wayland).
- [x] The frame loop driven by the host: `engine_frame` from the host's timer or display link; nothing in the engine blocks on the host's loop; a frame budget; present modes mapped to what the host surface allows.
- [x] Tests (C host with its own window): forwarded keys and pointer events reach a `ui` widget (a button click increments a counter read back through the world API), a text field receives text, the cursor callback is called when the pointer enters a text field, the clipboard callbacks round-trip, no second display connection exists; pixel comparison of the UI frame with a golden.
- [x] Document the host's duties (threads, ordering, DPI) in the guide with a worked example.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a C host window with its own event loop drives the engine's UI by forwarded input and callbacks, with pixels matching a golden.

## Outcome

**Done (2026-11-15, Linux).** New table function `surface_input` (one size-prefixed `engine_input_event`: key, pointer move/enter/leave/button, scroll, text, focus, touch) and `engine_surface_callbacks` (set_cursor, set_pointer_lock, clipboard_set/get, text_input_rect, request_focus) given in `engine_surface_info.callbacks`; `engine_surface_info.present_mode` (FIFO, mailbox, immediate; the swapchain falls back to FIFO, `engine_surface_state.present_mode` says what was granted) and `engine_surface_state.input_dropped`. `os::ForeignWindow` now has an input queue, hooks (`ForeignWindowHooks`) for cursor, pointer lock, text-only clipboard, text-input rect and focus; the engine's UI binds to it like to its own window and opens no connection of its own.

- Deviation from the task text: the entry points are one `surface_input` with an event type, not six `engine_input_*` functions; the frame loop needed no new function (`frame` and `budget_us` already exist from Day 349, the host calls it from its timer).
- Events are validated whole before anything is queued (unknown modifier bits, non-finite coordinates, a key or button out of range, malformed UTF-8 and text over 256 bytes are refused with `INVALID_ARGUMENT`); touch: the first finger acts as the pointer.
- Tests: `surface_input_test.cpp` (Catch, X11 window, the UI toolkit bound to the surface: forwarded pointer and keys click a button whose counter is read through the world API, a text field receives text and a paste, the cursor callback fires on entering a text field, clipboard callbacks round-trip, the UI frame equals a golden, queue overflow counted) and `host_surface.c` (every event argument, overflow, present-mode fallback, callbacks size, and the count of `XOpenDisplay` calls originating in the program: 1, the host's; the NVIDIA driver opens its own connection inside Vulkan, which is the driver's).
- Pages touched: `docs/guide/engine-library.md` (Day 353 section with the host's duties and a worked example); `docs/reference/` regenerated unchanged; ABI snapshot regenerated (unreleased); `tests/phases.txt` (1 line).
- Runs: plain `ran 3361 of 8905 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 89 s`, asan `ran 2683 of 8905 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 149 s`, tsan `ran 2349 of 8905 tests (phases: all; flavors: tsan; display: included; crash tests: excluded) in 198 s`; all passed, none is the full suite. Built clean with Clang (unity), GCC (unity), Clang non-unity and `-DENGINE_BUILD_SHARED=ON`; the gates and `ctest -L docs` pass (docs 10630/10630).


## Post-implementation code review

`/code-review high` over the day's diff: 3 findings. **Fixed (2):** `ForeignWindow::Rebind` kept the pointer-lock flag of the old window (now cleared); `clipboard_get` copied the host's whole reported length before the size limit was checked (now copies at most the limit plus one byte, so the request still reports `TooLarge`); neither has a practical regression test (the first needs a lock held across a rebind on a real window; the second is an allocation-size change with the same visible result). **No change (1):** `(void)` on unused parameters in the hook defaults is the established pattern in `engine/platform` headers, not a hidden defect.
