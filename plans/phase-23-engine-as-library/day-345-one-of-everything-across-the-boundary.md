# Phase 23 / Day 345 — One of Everything Across the Boundary

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Days 342 (the singleton list), 343.

The singletons and global state exist once; memory crosses no boundary; thread-locals are safe at unload.

## Tasks

- [ ] Work through the Day 342 singleton list: log channels and the log sink, the settings registry and its stores, the job scheduler and its thread-locals, the allocator and its statistics, the debug and crash handlers and their signal state, the type registries, `HeapAllocator::Default()`; each either lives only inside the engine library, or is passed in by pointer; fix the ones that are function-local statics in headers (inline variables in a header are duplicated when two libraries include it).
- [ ] A rule and a check: no `static` non-const object at namespace or function scope in a public header of `engine/` (a `platform_neutral_check`-style gate rule, `shared_state_check`, with an allow list and reasons).
- [ ] **The duplicated-singleton test** (`tests/engine_library/dup_singletons`): a second small shared library that links only the C API (or the loader) and a host that loads both; the second library sees the engine's settings registry, log and scheduler through the C API and creates none of its own; a variant that wrongly links the static foundation is the planted failure and is detected (two registries).
- [ ] Memory ownership: nothing allocated by one library is freed by another through the C API (a test that runs a C host under a malloc-interposing checker or asan's allocator mismatch detection).
- [ ] Thread-local destruction order at unload (the Phase 2 review's finding): a worker thread's `thread_local` objects are destroyed before the library can be unloaded; test with `dlclose` after a job run, under asan and tsan.
- [ ] The crash handler across the boundary: installed once by the engine, uninstalled at shutdown, the host's own handlers restored; a crash test inside a job of the shared engine writes its dump.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** two libraries and the host see one registry, one log and one scheduler; the shared-state gate passes; unload after jobs is clean under asan and tsan.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
