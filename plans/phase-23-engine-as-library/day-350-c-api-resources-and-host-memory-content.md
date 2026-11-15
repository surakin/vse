# Phase 23 / Day 350 — C API: Resources & Host-Provided Content

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Days 349 and Phase 22 (`HostMemoryBackend`, the manager).

The host supplies content from memory and loads it through the engine.

## Tasks

- [x] `engine_resources_add(engine*, id, path_or_null, bytes, size, flags, release_fn, user)`: copied, or borrowed with a release callback called exactly once, on the thread the spec names, after the last reader is done; `engine_resources_replace`, `engine_resources_remove` (`ENGINE_STATUS_IN_USE` with the dependents listed through an out buffer; an explicit `force` unloads them atomically), `engine_resources_mounted` (a query), a content-hash check flag; the memory backend mounted at a configurable priority (default above the pak).
- [x] Loading through the C API: `engine_resource_load(engine*, id_or_path, type, handle* out)`, release, a status query, the asynchronous form with a ticket (poll, wait, cancel) over `AsyncLoader`; CPU resource views are not exposed (no pointer into a blob), rendering uses them through the frame loop.
- [x] Minting entry points: decided on Day 342 (the planner's default: not exposed; the host adds minted blobs).
- [x] Tests (C host): add a mesh, a texture and a material from memory (blobs minted by `asset_mint` into the test's data), load them, **replace the texture while a frame is in flight**, remove a depended-on resource (refused with the dependents), force-remove, and unload the library with borrowed buffers released exactly once (counted); borrowed buffer freed early is the planted misuse the debug build detects.
- [x] Ownership comments in the header for every function (who frees what, which thread a callback runs on).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a C host adds, replaces and removes content from memory with exact ownership and release counts, and loads it synchronously and asynchronously.

## Outcome

**Done (2026-11-15, Linux).** 13 new table functions (`resource_add`, `resource_replace`, `resource_remove`, `resource_mounted`, `resource_load`, `resource_release`, `resource_status`, `resource_load_async`, `ticket_poll`, `ticket_wait`, `ticket_finish`, `ticket_cancel`, `ticket_destroy`), three structs (`engine_resource_add_info`, `engine_resource_info`, `engine_ticket_info`) and the setting `assets.memory_priority` (default 1000, above the mounts of `assets.mounts`).

- **Lent bytes** are a feature of `HostMemoryBackend` itself (`MemoryAddOptions::release`): the entry reads the host's memory in place and hands it back from its destructor, so "after the last reader" is the entry's lifetime (the backend, or an open reader of a replaced or removed resource). The C layer queues each hand-back and one job on the engine's job system runs them (`ResourceHost::RunReleases`), so the callback never runs on a host thread, never under a backend or manager lock, and `engine_destroy` waits for every pending one.
- **`engine_c/resources.*`** is the resource host (backend, manager with the CPU loaders, lazily made `AsyncLoader`, the handle and ticket tables, each an outstanding object of the engine); the handle helpers moved to `engine_c/handles.h` (kinds resource 3, ticket 4). The loader's owner-thread rule is kept by one mutex; objects are built by the host's calls (`frame`, poll, wait, finish), never by an engine thread on its own.
- Paths: the mounts' tables are read again lazily after a change, before the next path load.
- Not done, by design: paks and loose files are not mounted by the C API (only the memory mount); GPU resources need the renderer's C functions; a replace whose in-place reload fails keeps the old version and says nothing.
- Test: `tests/engine_library/host_resources.c` (`engine_c_resources`, plain, asan, tsan; blobs minted by `asset_mint` from the sample sources by `mint_resources_data.cmake`). It covers the bad calls of every function, copied and lent adds, dependents refused and forced, a texture replaced 24 times with a held handle while another thread runs frames, every release counted once and off the host thread, destroy with lent buffers outstanding, an overwritten lent buffer (`CORRUPT`), tickets (poll, wait, finish twice, failure, cancel, destroy early).
- Pages touched: `docs/guide/engine-library.md` (Day 350 section), `docs/reference/asset.md`, `symbols.txt`, `README.md` (the `asset` headers), `ARCHITECTURE.md`.
- Runs: see `STATUS.md` (plain, asan and tsan `--affected` lines are quoted there).

## Post-implementation code review

`/code-review high`: 4 findings. Fixed: (1) the release callback's restrictions were incomplete in the header (`ticket_wait` and blocking workers); (3) `resource_remove` did not say what a loaded copy does; (4) a call with a bad handle built the resource host (now `ResourcesIfCreated`, with a test: forged handles on an engine that made none). Declined: (2) a failed in-place reload after a replace is not reported (the bytes are installed, so failing the call would break "a failing call changes nothing"; the header says the old version stays loaded).

Also found while writing the test, before the review: the engine handle's kind byte is 0 and must reach `TYPE_MISMATCH` for a world call (`engine_c_runtime` caught a stricter check in the refactored `ResolveSlot`); the test's own bugs (a lent buffer had to be the bytes the host passes; mount tables must be read again after a change).

