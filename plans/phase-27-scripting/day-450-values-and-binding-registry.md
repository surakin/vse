# Phase 27 / Day 450 — Value Marshalling and the Binding Registry

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 446, 448.

How native engine functions and types become visible to scripts without per-binding boilerplate and without letting a script hold a dangling pointer.

## Tasks

- [ ] `script::BindingRegistry`: namespaces of functions with typed signatures (`f32`, `i32`, `bool`, `StringView`, `Vec3`, `Quaternion`, `Entity`, `Value`), documentation strings, a stable id per function, and a marshalling layer generated from the signature by templates (a wrong argument count or type is a Lua error with the function's name, never an abort or a read of garbage).
- [ ] Handles: entities and engine objects cross into scripts as generation-checked handles (an index and a generation), never a pointer; a handle to a destroyed entity fails every call with a clear error; handles are values that can be stored in tables and sent in events.
- [ ] Userdata types (`Vec3` and friends are by-value Lua cdata or small userdata chosen by the Day 444 spike; the spike's cost numbers decide, the choice and the reason are written here), metatables with operators, `tostring`, equality.
- [ ] Binding installation per state: a state is built from the registry's list (so a state can be created with a subset, such as the task state of Day 461 that has no world access).
- [ ] The binding documentation generator: the registry produces `docs/reference/script-api.md` (functions, signatures, descriptions, which bindings are `world`, `pure` or `thread-safe-anywhere`); `ctest -L docs` checks it is current.
- [ ] Every binding is marked with the thread rule it follows: `pure` (any state, any thread), `world-read` (reads the tick-start view), `world-write` (goes through the command buffer), `main-thread-only` (refused from a partition job with a clear error); the registry refuses to install a function without a mark.
- [ ] Tests: signatures, argument errors, handle generations, the marks, the subset install, 8 threads installing at once.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A native function becomes a script function from one typed declaration, bad calls become script errors, and every binding states its thread rule.
