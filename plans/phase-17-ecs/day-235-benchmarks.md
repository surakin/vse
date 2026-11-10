# Phase 17 / Day 235 — Benchmarks Against the Naive OOP Baseline

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Days 230-234.

The phase's definition of done: show the data-oriented layout actually wins, with honest numbers.

## Tasks

- [ ] **`game/ecs_bench`** (launch options only: `--entities N`, `--frames N`, `--threads N`, `--csv PATH`): the Day 222 benchmark definition implemented twice (ECS and OOP baseline, same work), warm-up frames discarded, median and p95 of per-frame time, the work checksum compared between the two implementations so a benchmark cannot win by doing less.
- [x] **Scenarios:** (1) iteration: the four-step workload at 1k, 10k, 100k, 1M entities, one thread; (2) parallel scaling of the heavy system at 1, 2, 4, 8 and N workers with efficiency; (3) structural churn: create+destroy 10% of entities per frame and add/remove a tag on 10%; (4) sparse access: a system touching 1% of entities by tag; (5) `Changed<T>` skipping: 1% of entities written per frame, a reader with `Changed`; (6) hierarchy propagation at 100k entities; (7) bytes per entity and creation/destruction throughput; (8) `perf stat` cache-misses and instructions per entity where `perf` is available (recorded, not required).
- [x] **Results** recorded in this file and in `docs/guide/ecs.md` ("Cost") with the machine, compiler, flags and commit; the pass condition from Day 222 is checked: 3x at 100k entities and 70 percent efficiency at 4 workers. **If a number misses**, profile, fix what the profile shows (candidates: chunk size, column prefetch, the iteration guard cost, write-tick marking, command-buffer growth, false sharing between per-job buffers), re-run, and record both runs; if it still misses, the result is filed in `KNOWN-ISSUES.md` with the numbers and the phase is not closed until the user accepts the miss.
- [x] **Sparse-set question from the spec** answered with scenario (4): added only if the sparse system is more than 2x slower than the OOP baseline's equivalent.
- [x] **Regression guard:** a reduced `ecs_bench` (10k entities, 30 frames) as a ctest `[benchmark]`-labelled test that fails on a check that the ECS is faster than the baseline (ratio only, not absolute time), so the claim is tested on every full run.
- [x] **Tests:** the baseline and the ECS compute the same checksum for every scenario at 1k entities (a unit test), the CSV writer round-trips, the churn scenario ends with `Validate` clean.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors asan on the unit tests (not on the timed runs); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (open: waits for the Windows run due on Day 236) (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md` "Cost" (tables).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the benchmark shows the ECS beating the naive OOP baseline by the pass condition (or the miss is filed with numbers and accepted by the user), results are published in the guide, and a ratio check guards the claim.

## Notes

- Files: `game/ecs_bench/{bench.h,bench.cpp,main.cpp,CMakeLists.txt}` (layer 14 in `modules.txt`); `tests/ecs/bench_test.cpp` compiles `bench.cpp` into `ecs_tests` (6 test cases; the ratio guard `[benchmark]` skips itself in debug and sanitized builds).
- Eighth scenario recorded as "change stamping" (read term against write term over the same pass: no measurable difference, 0.0588 ms both) because it is what Day 229 filed for this day; `perf stat` over the whole
  run (not per scenario): 793 M cache misses, 520 G instructions, 303 G cycles (IPC 1.7) at 100k entities, 50 frames: recorded, not a pass condition.
- Pass conditions (Release, 7950X3D, clang 23.1.4): iteration at 100k **5.6x** (needs 3x), parallel efficiency at 4 workers **85 percent** (needs 70). Both met on the first run, so nothing was profiled or changed.
- Sparse set: sparse access is 200x faster than the baseline; none added.
- Honest misses outside the pass conditions: churn 0.41x and hierarchy 0.67x of the baseline (full table in `docs/guide/ecs.md`, "Measured cost"); filed in `KNOWN-ISSUES.md`. `Changed<T>` with random writes gives 1.2x only (chunk precision).
- The hierarchy scenario builds 100k entities through `SetParent` (the O(world) call filed on Day 232) and the setup was not a problem at that size.
- Debug build numbers are meaningless (ECS 1.7x at 100k); only Release numbers are recorded.

## Post-implementation code review

`/code-review high` over the day's diff: 4 findings.

- **Fixed (1):** linking the plain `jobs` library into every `ecs_tests` flavor mixed it with `ecs_tsan`; three tsan `Parallel:` tests failed with data races until the link was removed (they pass again: `ecs_tests_tsan` 120 passed, 1 skipped).
- **Fixed (1):** dead `read_sum` accumulation in `RunChanged`.
- **Declined (1):** `using namespace engine_placeholder;` in `bench.h` (the demo headers do the same; it is not a documented module).
- **Recorded, not fixed (1):** churn (0.41x) and hierarchy (0.67x) are slower than the baseline's equivalent; outside the pass conditions, filed in `KNOWN-ISSUES.md`.

Runs: plain `ran 2109 of 5472 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (the two known X11 failures only); asan `ran 1592 of 5472 tests (phases: all; flavors: asan; ...)` clean; tsan `ran 1274 of 5472 tests (phases: all; flavors: tsan; ...)` with one failure: the hardware-GPU scene test aborts inside the ThreadSanitizer runtime (filed in `KNOWN-ISSUES.md`, not investigated). Clang and GCC builds with `-DENGINE_UNITY_BUILD=OFF` of `ecs_bench` and `ecs_tests` are clean; `ctest -L docs` 7 of 7; the gates (`module_deps_check`, `config_access_check`, `std_usage_check`) pass.
