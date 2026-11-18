# Phase 27 / Day 463 — Minted Script Assets II: Loading, Dependencies and the Resource Manager

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 462; Phase 22.

Scripts load through the resource manager like every other asset, with their dependencies, never as raw files at run time.

## Tasks

- [x] A `ScriptAsset` resource type registered with the resource manager: load, dependency closure loaded atomically before any instance starts (Phase 22's rule: no broken dependencies, no silent fallback), unload when unreferenced, asynchronous loading with the streaming budget.
- [x] Per-state compilation: each partition compiles a script asset once, on its first use, from the shared immutable source (or bytecode); the compiled form lives in that state only; a script load failure disables the instances that need it and reports by value.
- [x] The loader `require` of the sandbox (Day 449) resolves asset ids to the loaded dependency closure; a `require` of an id outside the script's declared dependencies is an error.
- [x] Script components refer to scripts by asset id; the resource server and the pak backend serve script blobs like others (tests through the filesystem backend and the pak).
- [x] The development loader: with `script.dev_directory` set, source files load from disk through the file layer by asset id mapping, for the hot reload of Day 464; absent in a shipped configuration (setting default off, a gate keeps it out of a release build's defaults).
- [x] Tests: a closure of five scripts, a missing dependency, a cycle, unload and reload, async load, equal behavior across thread counts.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Scripts and their dependencies load atomically through the resource manager and compile once per partition.

## Notes

The same-layer edge `script -> asset` is allowed in `modules.txt` and ARCHITECTURE.md. A script component still names a behavior (by-id components, the dev directory's reader, script bytecode loading and async-load tests are in KNOWN-ISSUES.md). A unity-build collision exposed by the new files (`Context` in two binding files) was fixed by renaming.

## Post-implementation code review

Findings: a script that waited at its top level was reported loaded but left half run (fixed: the load fails; test); a failed module was retried at the next checkout and reported 'defined twice' (fixed: the failure is recorded in the slot and repeated; test); the module host's globals can be replaced by scripts, and two scripts defining one behavior name are not detected (declined: one trust domain per state, recorded in KNOWN-ISSUES.md).

Runs: `scripts/test-phase.sh 27 --no-display`: plain `ran 198 of 10982 tests (phases: 27; flavors: plain; display: excluded; crash tests: excluded)`, asan `ran 191 of 10982`, tsan `ran 191 of 10982`; phase 22 plain `ran 451 of 10982`; gates 33/33; `ctest -L docs` 9/9; Clang and GCC, unity and non-unity, built clean (script_tests, scene_tests, asset_mint).
