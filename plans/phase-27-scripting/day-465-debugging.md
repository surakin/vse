# Phase 27 / Day 465 — Debugging: Errors, Hooks and the Console

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 448-449, 457-458.

A script author can see what went wrong and where, and stop the world to look.

## Tasks

- [ ] Error reporting: every script error logs `script name:line: message` with a stack trace of script frames (and the entity's name and id), once per distinct error per instance (the repeat count is shown); errors are also an engine event (`on_script_error`) and counted in the statistics.
- [ ] A debug hook API in the backend interface (line, call, return, error events; breakpoints by script and line; step over, into, out; inspect locals and `self` as `Value`s) implemented for LuaJIT through `lua_sethook` and `lua_getinfo`; **a breakpoint pauses the partition job that hit it, and the policy is explicit: by default the whole script phase waits (all partitions at their next sync point) so the world stays consistent; `script.debug_pause_all=false` pauses only that partition and the others continue to the sync point and wait there**; the main thread stays responsive.
- [ ] The debug transport is a plain interface (commands in, events out as `Value`s) with an in-process implementation used by tests; the network adapter for external debuggers is not in this phase (listed in Notes).
- [ ] The console: `script.console_run(partition or entity, chunk)` runs a chunk in a chosen partition's state at the next sync point (or in a scratch state) and returns the result or error as values; the UI console of Phase 15 can bind to it (a small widget in `ui_demo` is Day 475's, optional).
- [ ] Profiling counters per script and per function (call counts and time by `os::MonotonicTimeNanos` inside the hook only when enabled: never on by default), a top-N report; the hot-script warning of Day 458 uses it.
- [ ] Tests: error text and trace format (committed golden text), breakpoints and stepping with the pause-all policy on 8 threads (no deadlock, no race under tsan), the console, counters.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Errors show where they happened, breakpoints stop the world safely on any thread count, and a console runs code inside a live partition.
