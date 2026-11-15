# Phase 23 / Day 343 — The Shared Build on Linux

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Day 342.

The whole engine builds as one `libengine.so` with `-DENGINE_BUILD_SHARED=ON`; static stays the default and is not slower.

## Tasks

- [ ] Make every module target an object library (or a static library of PIC objects) when `ENGINE_BUILD_SHARED` is on and add one `engine` `SHARED` target that links them; position-independent code for all modules and the vendored C libraries (yyjson, zlib, libpng, stb, sqlite, freetype, sentry, libbacktrace) in both modes.
- [ ] SONAME and version (`libengine.so.1`), `RPATH`/`RUNPATH` for the build tree, an install rule; the unity builds keep working (an object per unity file) and `ENGINE_UNITY_BUILD=OFF` too.
- [ ] The engine's own tests still link the static libraries when the option is OFF; with it ON, a **second set** of tests links the shared library through the same CMake function (`engine_add_*_tests` gains a mode argument) so Day 356 can run the existing suites against the shared build.
- [ ] Sanitizer flavors: `engine_asan` and `engine_tsan` shared libraries with the runtime shared correctly (Spike 3's flags); a test program per flavor starts, runs a job and exits.
- [ ] `module_deps_check` and the other gates keep their rules (the aggregate target is one more `target` line); the gate reads the module list, not the link graph, so nothing changes in what it enforces.
- [ ] Measure: library size, start-up time to `main`, and the static build's `renderer_bench` and `ecs_bench` figures before and after the change (the static default must not get slower: the spec's tolerance); record in the spec.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** `cmake -DENGINE_BUILD_SHARED=ON` builds `libengine.so` (plain, asan, tsan) and a test program linking it runs; the static build's benchmark figures are unchanged within the tolerance.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
