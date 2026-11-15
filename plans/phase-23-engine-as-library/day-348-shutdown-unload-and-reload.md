# Phase 23 / Day 348 — Complete Shutdown, Unload & Reload

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Days 345, 347.

The engine can be shut down completely and the library unloaded and loaded again in one process.

## Tasks

- [ ] Audit shutdown: all threads joined, all registrations removed (`DeviceResources`, settings listeners, log sinks, signal handlers, atexit-style hooks), no static destructor that touches another static; a list in the guide of what `engine_destroy` guarantees.
- [ ] `engine_destroy` with outstanding objects (a world with entities, a loaded resource, an in-flight async load or import): documented behavior (refuse with `ENGINE_STATUS_BUSY` listing what, or an explicit `force` that cancels and waits), each tested.
- [ ] The reload cycle test: a C host loads, runs jobs, a headless frame, destroys, unloads, and repeats 50 times in one process, with the library built plain, asan (leaks reported at exit only for what is really leaked) and tsan; memory use returns to the baseline (a check on the process's resident size and on the allocator's statistics).
- [ ] `dlclose` hazards: the library marked `-Wl,-z,nodelete` is **not** used; a test proves the library really unloads (its symbols are gone from `/proc/self/maps`); the documented limit: a host must not unload while the engine is alive.
- [ ] Unload with other threads still inside (a host thread blocked in a callback): refused or documented, tested.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** 50 load-run-unload cycles in one process pass under asan and tsan, the library leaves the address space, and no resource of the engine remains.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
