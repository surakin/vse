# Phase 23 / Day 349 — C API: the Frame Loop, a World, Jobs & Crash Handling

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Days 346-348.

The runtime surface of the C API beyond create and destroy.

## Tasks

- [ ] The frame loop: `engine_frame(engine*, const engine_frame_info*)` driven by the host (delta time, a budget in microseconds); no engine thread blocks on the host's loop; frame statistics out.
- [ ] Worlds and entities at a basic level: `engine_world_create/destroy`, `engine_entity_create/destroy`, set and get of the built-in components (transform, name, mesh reference) through a typed, size-prefixed property API (no pointer into the world escapes), queries by component, entity keys; generations catch stale handles.
- [ ] Jobs: `engine_jobs_submit(engine*, fn, user, counter*)` and `engine_counter_wait`, with the rule that a job callback is C and may call the C API only through the table it was given; a worker-thread-safe subset of the API is listed.
- [ ] The crash handler: `engine_crash_handler_install/uninstall` with the dump directory from settings; a crash test of the shared engine inside a host process writes a dump and the host's own previous handler is restored on uninstall.
- [ ] Tests (C host): create a world, a thousand entities, set components, query, run a frame with a system, submit jobs and wait, destroy; stale handles fail; the same test under tsan from two host threads for the thread-safe subset.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a C host drives frames, creates entities and runs jobs through the table, with documented thread rules, under asan and tsan.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
