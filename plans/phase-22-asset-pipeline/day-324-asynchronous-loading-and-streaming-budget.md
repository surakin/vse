# Phase 22 / Day 324 — Asynchronous Loading, Cancellation & the Streaming Budget

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Day 323; Phase 2 (jobs, coroutines); Phase 19 Day 263 (`AsyncStorage`, the pattern for work that must not stall the frame).

Loading without stalling the frame: decode on workers, upload on the owner thread, bounded.

## Tasks

- [ ] **`LoadAsync<T>(id, priority, token) -> LoadTicket`:** reads and decodes on jobs (the backend's `ReadAtAsync` where it has one), then queues the GPU part for the owner thread's `Update(budget)`; `Wait`, `IsDone`, `Finish`, as `SaveAsync` does; cancellation by token before and during the read (the result is dropped and nothing leaks).
- [ ] **Priorities and a bounded queue:** three priorities (`Immediate`, `Normal`, `Background`); at most `assets.max_inflight` loads at once; a request over the limit queues, and over `assets.max_queued` returns `LimitExceeded` (back-pressure as a value).
- [ ] **Streaming budget:** `Update(max_microseconds, max_bytes)` does GPU uploads until the budget is spent, so a frame's cost is bounded; the leftovers wait for the next frame; a `Stats` counter reports the backlog.
- [ ] **Level loading as a streamed unit:** `LoadLevelAsync` loads the blob, then its dependency list in priority order, reporting progress (`loaded/total`); the level becomes active when every `Immediate` dependency is ready.
- [ ] **Deduplication:** two requests for one id share one load; a request for a resource that is loading, loaded or failed behaves consistently (a failed load is retried only on explicit request).
- [ ] **Tests** (asan and tsan matter here): many concurrent requests of the same and different ids, cancellation at every stage (stress), the budget bounding per-`Update` work (fake clock), queue limits, a level with 500 dependencies loading in priority order, shutdown with loads in flight (no leaks, no use after free), a device reset during loads.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 330; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** assets load on workers without stalling the frame, cancel cleanly at any stage, respect the budget and queue limits, and shut down with loads in flight under asan and tsan.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
