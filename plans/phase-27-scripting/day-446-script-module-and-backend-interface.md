# Phase 27 / Day 446 — The Script Module and the Language-Backend Interface

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 445; Phase 17.

The seam: what the engine asks of a language, with no mention of Lua. The LuaJIT backend and a test backend are two implementations of it.

## Tasks

- [ ] Create `engine/script/` (library `script`, above `jobs`, `ecs`, `settings`, `asset`; `ARCHITECTURE.md` entry) with `script/value.h`, `script/backend.h`, `script/status.h`.
- [ ] `script::Value`: nil, bool, i64, f64, string, vec2/3/4, quaternion, entity handle, byte blob, array and map of values (depth and size limits in the type), copied across states and threads by value; a hostile-input limit on depth and total size; no pointer into a state escapes.
- [ ] `script::Backend` (a factory: name, version, capabilities such as `jit`, `coroutines`, `bytecode`) and `script::State` (one language state, **owned by one thread at a time, handed over at a sync point**): `Load(chunk name, source)`, `Call(function path, args, results, budget)`, `RegisterFunction(name, native callback)`, `Collect()`, `MemoryUsed()`, `Close()`; every call reports `script::Status` (`Ok`, `SyntaxError`, `RuntimeError`, `BudgetExceeded`, `OutOfMemory`, `Invalid`, `Unsupported`) with a message and a stack trace as values, never an abort.
- [ ] The budget type (`instructions`, `nanoseconds`, `memory_bytes`) and what a backend that cannot honor one says (`Unsupported`, not silence).
- [ ] The coroutine hook of the interface: a call may *suspend* with a `WaitRequest` (seconds, frames, event name, task id) and be *resumed* later with values; backends without coroutines report it in their capabilities.
- [ ] Thread rules in the type: a `State` records its owning thread in debug builds and aborts on a use from another (`Aborts:` in the docs); the handover function is the one way to change owners.
- [ ] Tests with a scripted fake backend: the status paths, the value limits, the owner check (a death test), the capability reporting.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** `engine/script` exists with a documented, language-neutral backend interface, a value type that crosses threads, and tests against a fake backend.
