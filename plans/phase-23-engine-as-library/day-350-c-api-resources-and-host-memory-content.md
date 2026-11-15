# Phase 23 / Day 350 — C API: Resources & Host-Provided Content

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Days 349 and Phase 22 (`HostMemoryBackend`, the manager).

The host supplies content from memory and loads it through the engine.

## Tasks

- [ ] `engine_resources_add(engine*, id, path_or_null, bytes, size, flags, release_fn, user)`: copied, or borrowed with a release callback called exactly once, on the thread the spec names, after the last reader is done; `engine_resources_replace`, `engine_resources_remove` (`ENGINE_STATUS_IN_USE` with the dependents listed through an out buffer; an explicit `force` unloads them atomically), `engine_resources_mounted` (a query), a content-hash check flag; the memory backend mounted at a configurable priority (default above the pak).
- [ ] Loading through the C API: `engine_resource_load(engine*, id_or_path, type, handle* out)`, release, a status query, the asynchronous form with a ticket (poll, wait, cancel) over `AsyncLoader`; CPU resource views are not exposed (no pointer into a blob), rendering uses them through the frame loop.
- [ ] Minting entry points: decided on Day 342 (the planner's default: not exposed; the host adds minted blobs).
- [ ] Tests (C host): add a mesh, a texture and a material from memory (blobs minted by `asset_mint` into the test's data), load them, **replace the texture while a frame is in flight**, remove a depended-on resource (refused with the dependents), force-remove, and unload the library with borrowed buffers released exactly once (counted); borrowed buffer freed early is the planted misuse the debug build detects.
- [ ] Ownership comments in the header for every function (who frees what, which thread a callback runs on).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a C host adds, replaces and removes content from memory with exact ownership and release counts, and loads it synchronously and asynchronously.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
