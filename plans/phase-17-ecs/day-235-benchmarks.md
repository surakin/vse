# Phase 17 / Day 235 — Benchmarks Against the Naive OOP Baseline

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Days 230-234.

The phase's definition of done: show the data-oriented layout actually wins, with honest numbers.

## Tasks

- [ ] **`game/ecs_bench`** (launch options only: `--entities N`, `--frames N`, `--threads N`, `--csv PATH`): the Day 222 benchmark definition implemented twice (ECS and OOP baseline, same work), warm-up frames discarded, median and p95 of per-frame time, the work checksum compared between the two implementations so a benchmark cannot win by doing less.
- [ ] **Scenarios:** (1) iteration: the four-step workload at 1k, 10k, 100k, 1M entities, one thread; (2) parallel scaling of the heavy system at 1, 2, 4, 8 and N workers with efficiency; (3) structural churn: create+destroy 10% of entities per frame and add/remove a tag on 10%; (4) sparse access: a system touching 1% of entities by tag; (5) `Changed<T>` skipping: 1% of entities written per frame, a reader with `Changed`; (6) hierarchy propagation at 100k entities; (7) bytes per entity and creation/destruction throughput; (8) `perf stat` cache-misses and instructions per entity where `perf` is available (recorded, not required).
- [ ] **Results** recorded in this file and in `docs/guide/ecs.md` ("Cost") with the machine, compiler, flags and commit; the pass condition from Day 222 is checked: 3x at 100k entities and 70 percent efficiency at 4 workers. **If a number misses**, profile, fix what the profile shows (candidates: chunk size, column prefetch, the iteration guard cost, write-tick marking, command-buffer growth, false sharing between per-job buffers), re-run, and record both runs; if it still misses, the result is filed in `KNOWN-ISSUES.md` with the numbers and the phase is not closed until the user accepts the miss.
- [ ] **Sparse-set question from the spec** answered with scenario (4): added only if the sparse system is more than 2x slower than the OOP baseline's equivalent.
- [ ] **Regression guard:** a reduced `ecs_bench` (10k entities, 30 frames) as a ctest `[benchmark]`-labelled test that fails on a check that the ECS is faster than the baseline (ratio only, not absolute time), so the claim is tested on every full run.
- [ ] **Tests:** the baseline and the ECS compute the same checksum for every scenario at 1k entities (a unit test), the CSV writer round-trips, the churn scenario ends with `Validate` clean.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors asan on the unit tests (not on the timed runs); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md` "Cost" (tables).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the benchmark shows the ECS beating the naive OOP baseline by the pass condition (or the miss is filed with numbers and accepted by the user), results are published in the guide, and a ratio check guards the claim.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
