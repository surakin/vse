# Phase 17 / Day 231 — Parallel Scheduling Through the Job System

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Day 230; Phase 2 (`Scheduler`, `Counter`, `parallel_for`).

The stretch goal of the original plan, made a deliverable: independent systems run as jobs, one system's chunks run in parallel, and the result stays deterministic.

## Tasks

- [ ] **Conflict graph:** from the declared access sets, two systems conflict when one writes what the other reads or writes (components and resources); `Schedule::Build()` groups each stage into *waves*: the largest sets of mutually non-conflicting systems that respect the order constraints, found greedily in registration order (a later system never overtakes an earlier conflicting one, which preserves the sequential semantics).
- [ ] **Execution:** a wave submits one job per system through the job `Scheduler` and joins on a `Counter`; the frame thread helps (`HelpUntil`) as everywhere else in the engine; no thread of the ECS's own; with `ecs.parallel_systems` off, or no scheduler, the waves run inline in order (the same code path with a different submit).
- [ ] **Chunk parallelism inside one system:** `ForEachChunkParallel(fn, grain)` over the query's chunks through `ParallelForBlocking`/`ParallelFor`; a chunk is the unit of work (no two jobs touch one chunk); the grain defaults to a count that gives each worker a few jobs; the system's command buffer becomes one buffer per job taken from the `CommandBufferPool`, merged in chunk order at the end so playback is independent of the job schedule.
- [ ] **Determinism contract** (written in the guide): for systems whose results do not depend on iteration order, a frame's resulting world state is bit-identical to the sequential run; ordering-sensitive reductions use `ForEachChunk` with per-chunk partial results combined in chunk order (a helper `ReduceChunks<T>(init, per_chunk, combine)`).
- [ ] **Safety net:** in debug builds each system's declared access is enforced (Day 230), and a `ParallelAccessChecker` records, per wave, which component columns each running system touched; two systems of one wave touching the same column with a write is an assert naming both.
- [ ] **Tests** (`tests/ecs/parallel_test.cpp`, with the job scheduler at 1, 2, 4 and the machine's count): wave construction on hand-made access sets (disjoint writers share a wave, reader-after-writer does not, order is preserved); a pipeline of 8 systems over 200k entities gives a world state equal to the sequential run (compared by hashing all component bytes) over 50 frames and over 20 different thread counts/timings (`ENGINE_JOB_THREADS`); `ReduceChunks` equals the sequential sum exactly for floating point (fixed chunk order); command buffers from parallel chunks apply in chunk order; the access checker catches a deliberately wrong declaration; **tsan** runs the whole file; a stress test of 1,000 frames with 16 systems and random structural commands ends with `Validate` clean; scaling table (1, 2, 4, N workers) for one heavy system recorded here.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors tsan is mandatory for this day (and asan); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md`: "Parallel systems" (the contract, how to declare access, determinism rules); `docs/guide/jobs.md` cross-reference.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** independent systems and the chunks of one system run in parallel through the job system with a deterministic result equal to the sequential frame, verified under tsan and across thread counts.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
