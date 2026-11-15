# Phase 23 / Day 343 — The Shared Build on Linux

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Day 342.

The whole engine builds as one `libengine.so` with `-DENGINE_BUILD_SHARED=ON`; static stays the default and is not slower.

## Tasks

- [x] Make every module target an object library (or a static library of PIC objects) when `ENGINE_BUILD_SHARED` is on and add one `engine` `SHARED` target that links them; position-independent code for all modules and the vendored C libraries (yyjson, zlib, libpng, stb, sqlite, freetype, sentry, libbacktrace) in both modes.
- [x] SONAME and version (`libengine.so.1`), `RPATH` for the build tree (the install rule moves to Day 346 with the C header); the unity builds keep working (an object per unity file) and `ENGINE_UNITY_BUILD=OFF` too.
- [ ] (moved to Day 356, where the suites are run shared: `KNOWN-ISSUES.md`) The engine's own tests still link the static libraries when the option is OFF; with it ON, a **second set** of tests links the shared library through the same CMake function (`engine_add_*_tests` gains a mode argument) so Day 356 can run the existing suites against the shared build.
- [x] Sanitizer flavors: `engine_asan` and `engine_tsan` shared libraries with the runtime shared correctly (Spike 3's flags); a test program per flavor starts, runs a job and exits.
- [x] `module_deps_check` and the other gates keep their rules (the aggregate target is one more `target` line); the gate reads the module list, not the link graph, so nothing changes in what it enforces.
- [x] Measure: library size and start-up time (debug build: Release sizes on Day 356); the static default's figures need no re-measurement: no compile command of the OFF build changed (`ninja -n` rebuilds nothing); recorded in the spec.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** `cmake -DENGINE_BUILD_SHARED=ON` builds `libengine.so` (plain, asan, tsan) and a test program linking it runs; the static build's benchmark figures are unchanged within the tolerance.

## Outcome

Done (2026-11-15, Linux). `-DENGINE_BUILD_SHARED=ON` builds `libengine.so.1`, `libengine_asan.so.1` and `libengine_tsan.so.1` (`engine/engine_c/CMakeLists.txt`): the whole-archive of the 24 module static libraries of the flavor under a version script (one exported symbol, `engine_shared_probe`), `SONAME libengine.so.1`; every module and vendored library is compiled position independent with hidden symbols when the option is ON (set once at the top of the root `CMakeLists.txt`, which also declares the option once and requires CMake 3.24). A C program per flavor links the library and runs 2,000 jobs in it (`engine_shared_link_probe`, `_asan`, `_tsan`; skipped stubs when the option is OFF so the phase has the same flavors in every configuration, and judged by exit code). Built clean in unity and non-unity with Clang, and non-unity with GCC (GCC unity stops in `engine/net`, known). Sizes (debug): 25.3, 56.9 and 28.5 MB; load and scheduler start 2 ms. The static default is unchanged (nothing rebuilds). Registered: module `engine_c` (layer 14) in `module_deps_check` and `ARCHITECTURE.md`, the C probe in the `std_usage_check` allow list, the phase's flavors, `BUILD.md`, the guide, the spec's Day 343 section.

Runs: `scripts/test-phase.sh --affected` plain: `ran 3300 of 8817 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`; the failures were the two locked-session X11 tests and `std_usage_check`, which flagged the C probe's `stdio.h` (allow-listed, passes now); the shared tree's `engine_shared*` tests 4 of 4 (plain, asan, tsan); `ctest -L docs` 7 of 7.

## Post-implementation code review

`/code-review high`: 8 findings, 5 fixed, 3 recorded. Fixed: the probe tests used `PASS_REGULAR_EXPRESSION`, which ignores the exit code (sanitizer reports and teardown crashes would pass; now the exit code decides); no CMake version check for `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>` (3.24 is required with a clear error); nothing asserted the library list (every flavor must have the same count); the skipped stub always blamed the option (now says which); the `module_deps_check` lines sat inside the animation block. Recorded: `libengine` has hard `NEEDED` entries on Wayland, X11, xkbcommon, Xcursor, Xi and Xfixes (a headless host cannot load it: decide on Day 352 between loading them lazily and a separate window library), the global PIC and hidden-visibility settings also reach the static suites, tools and demos of an ON build (deliberate: one configuration to compare on Day 356), and the raw `__attribute__` on the one export (Day 344 replaces it with `ENGINE_API`).
