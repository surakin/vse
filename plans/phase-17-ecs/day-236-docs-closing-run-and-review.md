# Phase 17 / Day 236 — Docs, Windows PC, Closing Full Run & Phase Review

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Days 222-235.

Close the phase.

## Tasks

- [ ] **Guide pass:** `docs/guide/ecs.md` read end to end (concepts, a runnable hello sample, rules of thumb: when to use a tag, a `Changed` filter, the command buffer), tagged samples compile and run; `docgen lint` at zero; `cmake --build build --target docs`; `ctest -L docs`.
- [ ] **Windows PC:** pull; build `windows-msvc` (plain, asan) and `windows-clang-cl` in both unity modes; run the Phase 17 tests, the renderer demo on Direct3D 12 and Vulkan with validation (the goldens), and `ecs_bench` on Windows (numbers recorded, not gated); clear the "open until the other machine runs it" entries.
- [ ] **Closing full run (CLAUDE.md):** `scripts/test-phase.sh --all --flavor all` on Linux and on Windows (plain, asan), `ctest -L docs`, `renderer_demo`, `ui_demo`, `rhi_demo` 1000 frames with `--validation` on Wayland and X11; record each `ran X of Y tests (phases ...)` line here.
- [ ] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the whole phase's diff (Days 222-236, from the commit before Day 222 to the working tree), with extra attention to the concurrency and lifetime code (parallel scheduling, command buffers, chunk pool, removal tracking); verify, fix (with regression tests) or decline each finding; record the count and outcome under "Post-implementation code review".
- [ ] **Records:** `plans/phase-17-ecs.md` status complete, `PLAN.md` (roadmap row, log), `STATUS.md` (next: Phase 19, 3D text, whose day files exist; its Day 258 builds on `engine/scene`), `KNOWN-ISSUES.md` sweep (limits of this phase: 128 component types, 32 per archetype, conservative per-chunk change detection, no serialization, no events), `CURRENT_PHASE` stays 17 until Phase 19's first day starts; commit and push.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime or memory code (always for storage days); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the Linux and Windows full runs are recorded and pass, the benchmark claim is published and guarded, the uncapped review is resolved, and Phase 17 is marked complete.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
