# Phase 27 / Day 447 — A Second Backend and the Conformance Suite

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 446.

The seam is proven, not assumed: a tiny non-Lua backend passes the same suite the LuaJIT backend must pass.

## Tasks

- [ ] `script_expr`, a minimal backend (a small expression and statement language: numbers, strings, locals, `if`, `while`, function definitions, calls into registered natives, `wait(...)`) written against the interface only, in `engine/script/expr/`; it exists for tests and as the worked example in the guide, not for gameplay.
- [ ] The backend conformance suite (`tests/script/backend_conformance.h`, instantiated per backend): load and call, argument and result marshalling for every `Value` kind, errors as values, native registration, budgets (instruction and memory, skipped with a stated reason when `Unsupported`), coroutine suspend and resume (when advertised), state independence (two states never share globals), many states on many threads, a hostile corpus per backend.
- [ ] The suite runs for `script_expr` now and for the LuaJIT backend from Day 448; a backend registry (`script::RegisterBackend`, the same pattern as the RHI's and the resource backends') so a program lists the backends it has.
- [ ] A planted-fault test: a deliberately wrong fake backend fails the suite on the right case (the suite catches what it says it catches).
- [ ] Settings `script.backend` (default `luajit`) declared with `settings::Registry`; unknown backend names refused by value.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Two backends pass one suite, the registry lists them, and the seam is exercised by a second language.
