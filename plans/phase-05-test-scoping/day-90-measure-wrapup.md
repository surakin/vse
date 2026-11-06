# Phase 5 / Day 90 — Measurement, Full Run & Phase Wrap-up

**Phase:** [Phase 5 — Test Scoping](../phase-05-test-scoping.md)
**Depends on:** Day 89.

Prove the saving, prove nothing was lost, and close the phase with the first full run under the new rules.

## Tasks

- [x] Measure and record in this file: the full suite (all flavors, `-j16` and `-j1`) against the scoped runs for each phase (plain, and plain+ASan+TSan), with test counts; the saving as a ratio. Do the same for a typical day of Phase 6 work (a change in `engine/foundation/os` plus its tests, using `--affected`).
- [x] Plant a regression to check the safety net: break something in an earlier phase's module (for example a foundation allocator off-by-one) from a Phase 6-style diff, confirm `--affected` catches it through the touched-module rule, and confirm the closing `--all` run would catch a break the scoped run could not (a break in a dependency nobody edited). Record both; revert the plants.
- [x] Check that the scoped runs of Phase 6 and 7 will include what they need: list, for the Phase 6 and Phase 7 plans, the modules they will touch and the phases the runner selects for them; add any missing module to `tests/module_map.txt`.
- [x] Final full run, as the closing run of the phase: `scripts/test-phase.sh --all --flavor all` (plain, ASan, TSan), docs, GPU tests with validation, `renderer_demo` and `rhi_demo` for 1000 frames with validation on Wayland and X11 (a locked session breaks this; record it in `KNOWN-ISSUES.md` if unavailable); results in this file.
- [x] Mark Phase 5 complete in `plans/phase-05-test-scoping.md`, `PLAN.md` and `STATUS.md`; set `CURRENT_PHASE` to `6`; Phase 6 (Debugging facilities) needs its own day files before it starts.
- [x] **Documentation:** the measured numbers go into the guide's workflow section. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the phase's diff (Days 86-90), fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** the saving is a measured number, both planted regressions behaved as predicted, the closing full run passes in every flavor, `CURRENT_PHASE` is 6, and Phase 5 is marked complete.

## Result

**Measurements** (Ryzen 9 7950X3D, 16 threads, wall time, 2026-11-06, 1874 tests):

| Run | Tests | plain | plain + ASan + TSan |
|---|---|---|---|
| Whole suite, `-j16` | 1874 | - | 66 s |
| Whole suite, `-j1` | 1874 | - | 341 s |
| Phase 0 | 87 | 0.1 s | 0.1 s |
| Phase 1 (window tests) | 63 | 8.8 s | 8.8 s |
| Phase 2 | 77 / 232 | 1.8 s | 9.3 s |
| Phase 2.5 | 172 / 340 | 1.6 s | 1.9 s |
| Phase 3 | 226 / 580 | 17.3 s | 47.2 s |
| Phase 4 | 211 / 537 | 12.8 s | 43.5 s |
| Phase 5 | 35 | 1.4 s | 1.4 s |
| `--affected`, a Phase 6-style day in `engine/foundation/os` + `tests/foundation` | 871 / 1874 | 38 s | 68 s |
| `--affected`, a Phase 7-style day in `engine/rhi`, `engine/renderer`, `game/renderer_demo` | 638 | 26 s (dry-run count; timed on the RHI plant below) | - |

The saving is the ratio to the whole suite's 66 s: a day inside the newest phase about 47x (1.4 s), a renderer/RHI day 2.5x, a
foundation day 1.7x with the plain flavor (and none with all flavors, since the foundation is linked by everything and the
module map says so). Honest reading: the scoping pays off for work inside one phase, much less for foundation work.

**Planted regressions** (reverted afterwards; each run rebuilt first):

1. `ArenaAllocator::Alloc` with `offset_ = aligned + size - 1` (overlapping allocations), from a diff in `engine/foundation`:
   `--affected --since HEAD` selected phases 0 1 2 2.5 3 4 5 (871 tests) and failed (the arena test, and the docgen samples that use it).
   Two milder plants in the same function (`offset_ + align` over-aligning, `aligned + size + 1`) were **not** caught by any test: filed in `KNOWN-ISSUES.md`.
2. `HandlePool::Free` no longer bumping the generation (`engine/rhi/handle_pool.h`, a phase 3 module): `--current` (35 tests) passed, so the
   scoped current-phase run alone misses it; `--affected --since HEAD` (638 tests) and `--all` (871 plain) both failed 3 tests.

**Phase 6 and 7 plans against the module map:** Phase 6 touches `engine/foundation` (assert, log, a new debug/crash area), `engine/jobs`
(handlers on worker threads), `engine/rhi` (device-lost traces) and probably a new `tools/` directory; the first three are in the map (the
foundation selects every phase), a new directory forces the full suite until it gets a line, which is the safe behavior. Phase 7 touches
`engine/rhi`, `engine/renderer`, `game/renderer_demo`, `game/rhi_demo`, `shaders`: all mapped (2.5, 3, 4). No map change was needed.

**Closing run** (`scripts/test-phase.sh --all --flavor all --jobs 16`): `ran 1874 of 1874 tests (phases: all; flavors: all; display: included) in 67 s`,
100% passed (4 skipped as before: no `ENGINE_X11_TEST_LAYOUT`, a throughput test under sanitizers and one allocation test under TSan). `ctest -L docs`
4/4; `docgen lint` unchanged (no header changed), `docs/reference/` unchanged. `rhi_demo` and `renderer_demo` `--frames 1000 --validation` on Wayland
and on X11: no validation, error or warning lines; memory flat (301472 to 301520 KiB, 312488 to 312892, 310696 to 310876, 320656 to 320852).
`CURRENT_PHASE` is 6; Phase 5 is marked complete in its plan file, `PLAN.md` (roadmap row and progress log) and `STATUS.md`.
