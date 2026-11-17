# Phase 27 / Day 448 — The LuaJIT Backend I: States, Loading, Calls and Errors

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 445-447.

The first real backend: one `lua_State` per `script::State`, with the engine's allocator under it.

## Tasks

- [ ] `engine/script/luajit/` implements `script::Backend`: `lua_newstate` with a custom allocator that counts bytes against the memory cap and draws from the engine's heap (never `malloc`); the state's registry holds the engine's bookkeeping, nothing global in C.
- [ ] Loading: source text through `luaL_loadbuffer` (never `loadfile`), chunk names that are asset ids or test names, syntax errors as `SyntaxError` with line and message; calling a function by a dotted path with `Value` arguments and results through a marshalling layer that bounds depth and size both ways.
- [ ] Errors: `lua_pcall` with a message handler that builds a stack trace (function, source name, line) as values; a Lua error never unwinds through engine frames (exceptions are off: `lua_cpcall` and the LuaJIT C API's longjmp are contained inside the backend's own frames, and the build checks it).
- [ ] Native registration: `RegisterFunction` puts a C callback into a named table; the callback gets a marshalled argument view and returns `Value`s or an error; a native that fails returns a Lua error to the script, not an abort.
- [ ] The conformance suite passes for the LuaJIT backend (the budget cases are Day 449's: skipped by their `Unsupported` result today, recorded).
- [ ] State independence and thread handover: states created on one thread run on another after the handover; tsan clean in the 8-thread state test.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A LuaJIT state loads a chunk, calls into it, reports errors with stack traces and runs on any thread one at a time, passing the shared suite.
