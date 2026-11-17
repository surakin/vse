# Phase 27 / Day 463 — Minted Script Assets II: Loading, Dependencies and the Resource Manager

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 462; Phase 22.

Scripts load through the resource manager like every other asset, with their dependencies, never as raw files at run time.

## Tasks

- [ ] A `ScriptAsset` resource type registered with the resource manager: load, dependency closure loaded atomically before any instance starts (Phase 22's rule: no broken dependencies, no silent fallback), unload when unreferenced, asynchronous loading with the streaming budget.
- [ ] Per-state compilation: each partition compiles a script asset once, on its first use, from the shared immutable source (or bytecode); the compiled form lives in that state only; a script load failure disables the instances that need it and reports by value.
- [ ] The loader `require` of the sandbox (Day 449) resolves asset ids to the loaded dependency closure; a `require` of an id outside the script's declared dependencies is an error.
- [ ] Script components refer to scripts by asset id; the resource server and the pak backend serve script blobs like others (tests through the filesystem backend and the pak).
- [ ] The development loader: with `script.dev_directory` set, source files load from disk through the file layer by asset id mapping, for the hot reload of Day 464; absent in a shipped configuration (setting default off, a gate keeps it out of a release build's defaults).
- [ ] Tests: a closure of five scripts, a missing dependency, a cycle, unload and reload, async load, equal behavior across thread counts.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Scripts and their dependencies load atomically through the resource manager and compile once per partition.
