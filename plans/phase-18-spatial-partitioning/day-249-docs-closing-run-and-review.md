# Phase 18 / Day 249 — Docs, Windows PC, Closing Full Run & Phase Review

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Days 237-248.

Close the phase: complete guide, the Windows run, every flavor, and the uncapped review.

## Tasks

- [x] **Guide:** `docs/guide/spatial.md` complete and readable by a junior developer (overview, which structure when, queries, the ECS tier, the physics and renderer contracts, rules, common mistakes, see also), every tagged sample compiling and running; `docs/guide/README.md` lists it; `docs/guide/ecs.md` and `renderer.md` point to it; a tutorial `docs/tutorials/spatial-queries.md` (add entities, ask for neighbors and a ray, see the result) with a sample that runs.
- [x] **Windows PC (done 2026-11-11):** build both compilers and both unity modes; run `tests/spatial`, `tests/scene` and the demo smoke test there; the two-machines entries in `KNOWN-ISSUES.md` for Days 237-248 are closed or filed with the facts.
- [x] **Closing full run:** `scripts/test-phase.sh --all --flavor all` (on Linux; plain and asan on Windows), plus `ctest -L docs` and the phase's demo; the final `ran X of Y tests (...)` lines recorded here; `CURRENT_PHASE` stays 18 until Phase 19's first day.
- [x] **End-of-phase review (uncapped, CLAUDE.md):** `/code-review high --max-findings all` over the whole phase's diff (the commit before Day 237 to the working tree); verify, fix or decline each finding with a reason; record the count and the outcome here and in `STATUS.md`.
- [x] **Plan follow-through:** `plans/phase-22-physics.md` is re-read against what was built and amended where the broad phase is now Phase 18's; the open items (tile light lists, GPU culling, streaming cells) are filed in `KNOWN-ISSUES.md` or in the Phase 33 file.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (the whole suite, every flavor); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"; the Windows PC run of 2026-11-11):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide, the tutorial and the module list.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Phase 18 is complete: the spatial module is documented, measured, running on both machines, and reviewed.

## Notes

- Guide: `docs/guide/spatial.md` brought current (status line, stale "later" wording, the measured sync cost, "Measuring and debugging"); tutorial `docs/tutorials/spatial-queries.md` added to the tutorials index (its sample is run and its output compared by `docgen samples`); `ecs.md` and `renderer.md` already point to the guide (a stray `#` in renderer.md fixed). Design spec section 4.1 records the measured pass condition and corrects claims for features not built.
- **Windows PC (2026-11-11):** after the pull, all four Windows trees (`build-msvc` plain and asan, `build-clang-cl`, `build-windows-msvc-nounity`,
  `build-windows-clang-cl-nounity`) built clean with nothing to fix. Closing run: `ran 3918 of 3918 tests (phases: all; flavors: all (plain and asan, no tsan on Windows); display: included; crash tests: included) in 318 s`; one failure,
  `Throughput scales with participant count` (phase 2, the known timing flake), which then passed 3 times in 3. All 163 `phase18` tests pass
  (`tests/spatial`, `spatial_demo_runs`), and the `tests/scene` tests (visibility, render sync, cost, rendering) in both flavors; `ctest -L docs` 7 of 7.
  The spatial benchmark in Release (`build-msvc-release`, MSVC 14.51, Ryzen AI 7 350; `spatial_tests "[spatial-bench]"`, recorded, not gated; every
  answer checked): static build 0.40 / 3.9 / 41.7 / 576 ms for 1k / 10k / 100k / 1M items; frustum cull with about 1 % visible 30x / 86x / 86x / 115x
  faster than the flat scan (100k: 0.0135 ms against 1.16 ms); 40-unit overlap 1.7 / 2.8 / 4.2 / 12.4 us and closest ray 0.35 / 0.53 / 0.82 / 1.81 us per
  query; moving 1 % of 100,000 costs 0.92 us per item on the tree and 0.07 us on the grid, moving all of them 1.26 and 0.16 us; the pair update with
  everything touched 99.6 ms (tree) and 85.1 ms (grid); the ECS sync of 100,000 all-moving agents 259 to 264 ms with 1, 2, 4 and 8 workers alike (the
  open pass condition the Linux run filed, 156 ms there); the tree's margin sweep 374 ms a frame without margin, 116 ms at 0.25 with predictive 2 (the
  default), 57 ms at 1.0.
- **Windows PC, after 6ba686c7** (each set's moves as their own job; 2026-11-11): the four Windows trees build it clean; `--affected` plain
  `ran 2007 of 3918 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 313 s` (five clipboard and drag-and-drop tests failed opening the clipboard while the user was on Remote Control,
  then passed 3 times in 3: filed), asan `ran 1644 of 3918 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 136 s`, all passed. The sync benchmark (Release, two runs): 240 ms
  with 1 worker, 131 to 132 ms with 2, 4 and 8 (before: 259 to 264 ms at every count); the 80 ms target stays missed.
- **Windows PC: originally not done** (Day 249 ran on the Linux machine). The Linux-side entries for Days 237-248 stay open in `KNOWN-ISSUES.md` until the Windows PC builds `windows-msvc` (plain, asan) and `windows-clang-cl` in both unity modes and runs `tests/spatial`, `tests/scene` and `spatial_demo_runs`. The phase is complete on Linux; it is not closed until that run.
- Closing full run, Linux: `ran 5719 of 5719 tests (phases: all; flavors: all; display: included; crash tests: included) in 204 s`; failures: the two known X11 window tests and the tsan hardware-GPU scene test (both in KNOWN-ISSUES.md). `ctest -L docs` 7 of 7; docs 7675/7675, lint at zero. Clang and GCC non-unity builds clean.
- Plan follow-through: `plans/phase-22-physics.md` already names Phase 18 as its broad phase; the open items are filed in KNOWN-ISSUES.md (tile light lists, GPU culling, streaming cells in Phase 33, parallel static build).
- Pass-condition outcome (design spec section 4.1): met are culling, ray and small-box overlap queries, 1 percent moving, exactness. **Missed or open, for the user:** 100,000 all-moving agents (156 ms against 80 ms, 1.0x with 8 workers; the user chose per-set parallel moves, done after the day and recorded at the end of this file), the 40-unit overlap (2.7 us against 2 us), a timed quiet-scene sync (not measured).

## Post-implementation code review

Uncapped end-of-phase review (the whole phase diff, `bd1bc7b` to the working tree): four reviewers by area (structures and geometry; queries and pairs; the ECS tier and concurrency; the renderer, tooling and docs). About 45 findings; no high-severity bug.
- **Fixed with a regression test (2):** `TransformBounds` scaled a bounding sphere by the largest column length, which under-estimates for a non-uniform scale applied over a rotation (a visible object could be culled): it now uses the largest singular value (test `the sphere covers the largest stretch ...`, fails without the fix by arithmetic: 1.58 against 2). A membership edit that coincided with a box the sync refuses was dropped for good: removals are now applied without a box (test `a membership edit that coincides with a box the sync refuses ...`, fails without the fix, checked).
- **Fixed (no test needed or practical):** `FillOverlaps` is clamped to the slots counted for each query; `ApplyMoves` refuses a hand-written non-finite `WorldBounds`; a subnormal ray direction is treated as parallel in `Intersect(Ray, Aabb)`; dead `(void)` code removed from the benchmark; the tutorial's expected output corrected (the reviewer was right: `3 and 100`) and placed so the docs check compares it; the spec and guide corrected where they claimed things not built or measured (renderer contract, visible-list overload, `renderer.culling`, the quiet-scene and memory rows, the 40-unit overlap, stale figures); the `max_t` contract wording.
- **Declined or deferred, each filed with its reason in KNOWN-ISSUES.md ("Phase 18 end-of-phase review"):** performance or hardening limits of the grid (cells never freed, cubic oversized loop), `RemovePending`, the whole-tree static rebuild policy, the unproven dynamic-tree depth bound, the removal-log dependency, documented-contract items (pair margins, public `meets`, `items_tested` meaning), cosmetic debug-draw items, and unreachable cases (21-bit sentinels, unbounded boxes).

## After Day 249: parallel moves per set (the user's choice for the missed all-moving target)

`ApplyMoves` (`engine/spatial/spatial_sync.cpp`) lists the moved entities in chunk order (box plus the proxy's handles), then each set applies the list to itself as one `SystemContext::ParallelFor` job (`SetParallel(false)` or no scheduler: the same loop inline). A set is written by one thread in list order and sets share no state (checked: no statics or thread-locals in `engine/spatial`), so the result equals the sequential one; the existing test "the parallel path gives the same sets as the sequential one" now also compares `items_reinserted` and runs under TSan.

- Measured (Release, same machine and session, 100,000 all-moving agents, two sets, ECS sync only): before 175-185 ms with 1 worker and 180-196 ms with 2-8 workers; after 172-177 ms with 1 worker and 92-94 ms with 8 workers (1.9x; 2 workers 117-133 ms). An early run showed 240 ms single-threaded, which did not reproduce (machine noise; an instrumented run put the list building at 0-3 ms). The 80 ms single-thread target stays missed; the speedup is bounded by the set count.
- Runs: `scripts/test-phase.sh --affected` plain `ran 2192 of 5719` (the two known X11 window tests), asan `ran 1674 of 5719`, tsan `ran 1356 of 5719` (the known scene abort); GCC non-unity `spatial_tests` builds clean; docs 7675/7675, `ctest -L docs` 7 of 7; gates pass.
- Review (`/code-review high`, 2 findings, both low): (1) `refused_bounds` for a hand-written non-finite `WorldBounds` now counts every item the entity holds, where before it counted only live items whose box differed: accepted (the counter is a diagnostic; the guard test still passes). (2) The test cannot tell the parallel jobs from the sequential loop by itself: declined, the equality of sets and counters under 4 workers and under TSan is the check; the comment was corrected.
