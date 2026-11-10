# Phase 17 / Day 230 — Systems & the Sequential Scheduler

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Days 227-229.

System registration, access declaration, stages and the frame loop, running on one thread.

## Tasks

- [x] **`ecs::SystemDesc`:** a name, a function (`void(*)(SystemContext&)` or a lambda stored in a small inline buffer with a heap fallback counted), declared access (`Reads<...>`, `Writes<...>`, `ReadsResource<...>`, `WritesResource<...>`) deduced from the query terms when the system is built from a query (`MakeSystem("move", query, fn)`), a stage, optional `after`/`before` names, run condition (`RunIf(fn)`).
- [x] **`SystemContext`:** the world (restricted view: queries and resources only, no structural calls), `dt`, the system's own `CommandBuffer`, the frame number, a debug check that the system touches only what it declared (a query term or resource access outside the declared set is an assert in debug builds, a counted warning in release).
- [x] **`ecs::Schedule`:** `Add(SystemDesc)`, stages (`kPreUpdate`, `kUpdate`, `kPostUpdate`, `kRender` by default, user stages allowed by name), ordering constraints turned into a stable topological order (cycles are rejected at `Build()` with the cycle named; ties break by registration order), `Run(World&, dt)`: for each stage in order, run its systems, then apply the command buffers in registration order and `World::Advance()` the tick at the end of the frame.
- [x] **Per-system statistics** (setting `ecs.stats`): last and average nanoseconds, entities visited, chunks skipped by `Changed<T>`; read through `Schedule::Stats()`; zero cost when off (one predictable branch).
- [x] **Setting declaration:** `ecs.parallel_systems` and `ecs.stats` with `settings::Registry` (env and flag names), read through `Setting<T>`; run-time changes apply at the next frame boundary.
- [x] **Tests** (`tests/ecs/schedule_test.cpp`): order follows stages then constraints then registration; a cycle is rejected with its path; a system with a failed run condition is skipped; commands from systems apply after their stage in registration order (a destroy in system A and an add in system B on the same entity: the documented outcome); a system touching an undeclared component trips the debug check (death test); statistics count visited entities; the settings are read; a 10-system pipeline over 100k entities runs for 100 frames with `Validate` each 10 frames.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors asan (the system storage and the command buffers); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md`: "Systems and schedules".
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** systems declare their access, run in a deterministic stage and constraint order with deferred structural changes, and have optional statistics and settings.

## Notes

- `ecs/schedule.h`: `SystemDesc` (chained `Reads/Writes/ReadsResource/WritesResource/InStage/After/Before/RunIf`), `MakeSystem`, `MakeQuerySystem<Terms...>` (access deduced from the terms; the query is kept and rebuilt if the schedule runs another world, by `World::Id()`), `SystemContext`, `Schedule` (`AddStage`, `Add`, `Build` -> `BuildReport`, `Run`, `StatsOf`, `Stats`), `RegisterEcsSettings` (`ecs.parallel_systems`, `ecs.stats`).
- A system function is stored inline up to 64 bytes, else on the heap (`Schedule::HeapFallbacks()`); the plan's `void(*)(SystemContext&)` plus lambdas is one type (`detail::SystemFn`).
- Command buffers apply after each stage in **registration** order (not run order); `World::Advance()` ends the frame.
- **Deviations:** (1) a system touching something undeclared is reported with `ENGINE_ENSURE` and counted (`access_violations`), not aborted, so the test needs no death-test harness (not linked into `ecs_tests`); (2) run-time setting changes are read at the start of each frame; `ecs.parallel_systems` is declared and read but only the Day 231 scheduler uses it; (3) the restricted context offers `Query`, `Resource`/`ReadResource`, `Commands`, `Exists`, `Dt`, `Frame`, not `Get`.
- `engine/ecs` now includes the settings headers (foundation); `CMakeLists.txt` links `foundation_settings` on Windows too.
- Open: the Windows build and run ("Two machines").
- Runs: `ran 2061 of 5328 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the two known X11 failures), asan `ran 1544 of 5328`, tsan `ran 1226 of 5328`; GCC and Clang non-unity clean; `ctest -L docs` 7 of 7.

## Post-implementation code review

`/code-review high`: 3 findings. Fixed 2 with regression tests that fail without the fix: a query system stayed bound to its first world (test keeps both worlds alive; added `World::Id()`), and query counters gathered while statistics were off showed up in the first timed run. Declined 1: the undeclared-access check continues instead of aborting (see Deviations).
