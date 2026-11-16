# Phase 25 / Day 400 — Benchmarks, Stress and the Limits

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Days 395, 398, 399.

Measure what the phase built, set thresholds, and look for what breaks at scale.

## Tasks

- [ ] Benchmarks (`bench/physics_bench`, run on request, not in ctest): step time against body count (1k, 5k, 20k, 50k) awake and asleep; contact count against solver time; mesh world with 1M triangles; many joints; ray cast throughput (10^6 rays); character controller count; per-phase breakdown (broad, narrow, solve, integrate, write-back) from `PhysicsStats`.
- [ ] Thresholds recorded in the guide as regression targets of the day (a step of 5k awake bodies under a budget the spec set on Day 373, scaling with threads); a `physics_perf_smoke` ctest runs a small version with a generous bound so a gross regression fails.
- [ ] Stress and robustness: 100k random bodies dropped in a box (no NaN, no explosion, bounded energy), huge mass ratios (1:10^4), stacks to 50, degenerate shapes, bodies created inside each other, a body teleported 10^6 m away (no overflow in the broad phase), 10^7 steps of a single body (no drift in the quaternion norm), a fuzz of the C API with random calls (no crash, tsan/asan clean).
- [ ] Memory: the allocation counters after warm-up show zero allocation per step; the peak memory for 50k bodies recorded.
- [ ] Findings that are limits (a huge island runs on one thread, no graph colouring; mesh colliders are static only; no one-way platforms; no soft bodies) are in the guide's 'Limits' section and in `KNOWN-ISSUES.md` where they need a decision.
- [ ] The pair feed: `PairTracker::Update` on 5000 moving boxes with 2000 statics against the Day 373 reference (4.7 ms single-threaded, margin 0.1, all moving; 1.0 ms with 20 percent moving); if it is the limit of the 5k-body step, parallelising the tracker's gather (disjoint output per chunk, merged in key order) is a Phase 18 extension decided here and recorded.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The benchmark numbers and thresholds are recorded, the stress runs finish without NaN, explosion or crash, and the limits are documented.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
