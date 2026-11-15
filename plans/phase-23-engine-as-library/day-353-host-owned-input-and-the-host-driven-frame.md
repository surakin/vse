# Phase 23 / Day 353 — Host-Owned Input & the Host-Driven Frame Loop

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Day 352.

Input comes from the host; cursor, clipboard and focus requests go back as callbacks; the host drives frames from its own timer.

## Tasks

- [ ] Input forwarding: `engine_input_key`, `_pointer`, `_scroll`, `_text`, `_focus`, `_touch` (size-prefixed event struct with a timestamp and the modifiers) into the engine's input stream (`platform` input layer), so the UI toolkit, the camera controllers and the demos see them as from their own window; an engine-created window stays the source for engine windows only.
- [ ] Requests back: the engine asks for cursor shape, cursor visibility and lock, clipboard get and set, text-input rectangles and focus through callbacks registered at create; on an embedded surface the engine opens no input or clipboard connection of its own (tested by watching for the connection on X11 and Wayland).
- [ ] The frame loop driven by the host: `engine_frame` from the host's timer or display link; nothing in the engine blocks on the host's loop; a frame budget; present modes mapped to what the host surface allows.
- [ ] Tests (C host with its own window): forwarded keys and pointer events reach a `ui` widget (a button click increments a counter read back through the world API), a text field receives text, the cursor callback is called when the pointer enters a text field, the clipboard callbacks round-trip, no second display connection exists; pixel comparison of the UI frame with a golden.
- [ ] Document the host's duties (threads, ordering, DPI) in the guide with a worked example.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a C host window with its own event loop drives the engine's UI by forwarded input and callbacks, with pixels matching a golden.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
