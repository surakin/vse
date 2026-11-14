# Phase 22 / Day 330 — Asynchronous Loading, Cancellation & the Streaming Budget

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 328-329; Phase 2 (jobs, coroutines); Phase 19 Day 263 (`AsyncStorage`, the pattern for work that must not stall the frame).

Loading without stalling the frame: decode on workers, upload on the owner thread, bounded.

## Tasks

- [x] **`LoadAsync<T>(id, priority, token) -> LoadTicket`:** reads (straight into the aligned destination buffer, ranged and, where the backend can, asynchronously) on jobs (the backend's `ReadAtAsync` where it has one), then queues the GPU part for the owner thread's `Update(budget)`; `Wait`, `IsDone`, `Finish`, as `SaveAsync` does; cancellation by token before and during the read (the result is dropped and nothing leaks).
- [x] **Priorities and a bounded queue:** three priorities (`Immediate`, `Normal`, `Background`); at most `assets.max_inflight` loads at once; a request over the limit queues, and over `assets.max_queued` returns `LimitExceeded` (back-pressure as a value).
- [x] **Streaming budget:** `Update(max_microseconds, max_bytes)` does GPU uploads until the budget is spent, so a frame's cost is bounded; the leftovers wait for the next frame; a `Stats` counter reports the backlog.
- [x] **Loading by path waits for the table (added 2026-11-13):** `LoadAsync(path, ...)` resolves through the mount table; while a mount's table is still arriving (`RefreshTablesAsync`, Day 315) the ticket stays pending and completes when it is in, or fails with `NotFound` once every table is in and the path is not; a load by id never waits.
- [x] **Level loading as a streamed unit:** `LoadLevelAsync` loads the blob, then its dependency list in priority order, reporting progress (`loaded/total`); the level becomes active when every `Immediate` dependency is ready.
- [x] **Deduplication:** two requests for one id share one load; a request for a resource that is loading, loaded or failed behaves consistently (a failed load is retried only on explicit request).
- [x] **Tests** (asan and tsan matter here): many concurrent requests of the same and different ids, cancellation at every stage (stress), the budget bounding per-`Update` work (fake clock), queue limits, a level with 500 dependencies loading in priority order, shutdown with loads in flight (no leaks, no use after free), a device reset during loads.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [x] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 340; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** assets load on workers without stalling the frame, cancel cleanly at any stage, respect the budget and queue limits, and shut down with loads in flight under asan and tsan.

## Outcome

`engine/asset/async_loader.{h,cpp}`: `AsyncLoader` (over a `ResourceManager` and a `jobs::Scheduler`), `LoadTicket`, `LoadPriority`, `LoadUpdate`, `AsyncLoadStats`. `LoadAsync<T>`, `LoadPathAsync<T>`, `LoadLevelAsync`, `Update(max_microseconds, max_bytes)`, `Wait`, `Stats`. The manager gained `ReadBlob` (a read on any thread without the build lock), `IsLoaded`, `MayBeLoaded`, `LoaderKeyOf`, `LoadPrefetched`/`LoadPrefetchedRaw` (a load that takes its closure's blobs from a `PrefetchedBlobs` first), `Resolve`, `TablesReady`, `Config`, and the settings `assets.max_inflight` (default 4) and `assets.max_queued` (default 256).

Design: a reading job (on a worker) reads the blob and the blobs of its closure that are not loaded (a failed read is left for the build to report with its chain or fallback); `Update` on the owner thread builds the closure with the manager's ordinary loaders, taking the blobs from the prefetch. Two deviations from the plan, recorded in `KNOWN-ISSUES.md`: the reading job uses `ResourceReader::ReadAt` on the worker, not `ReadAtAsync`; and a level and its dependencies are built together (closure-first atomic loading, Day 328), so `LoadLevelAsync` reports read progress and reads the included levels first, and "active" is the level's ticket being done.

Runs: see `STATUS.md`.

## Post-implementation code review

`/code-review high` over the day's tree: 9 findings. Fixed (with regression tests that fail without the fix where practical): `Wait` on a queued or by-path ticket hung on a scheduler of one participant (it helped only when the ticket was reading); a new request joined a cancelled load and ended `Cancelled`, and joined one with another cancel token; cancelled requests counted against the queue limit; two requests by one path read the closure twice and a loaded path read it again (a follower now ends with its leader, a loaded resource is answered at once); `LoadTicket::Cancel` raced on a plain flag (now atomic); the reading workers waited behind the build through the manager's lock (the read counters have their own lock and `MayBeLoaded` does not wait); quadratic scans in the reading job (a head index and a hash set) and in the prefetch lookup (a hash index); the tests spun without a deadline (now `REQUIRE`d waits). Declined: the linear scan of the active list on each submit and of the ready and queued lists on each start (bounded by `assets.max_queued`, 256 by default; a map would need removal bookkeeping for no measured gain). The `STATUS.md` finding was the day's own last step.
