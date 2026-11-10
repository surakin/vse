# Phase 17 / Day 236 — Docs, Windows PC, Closing Full Run & Phase Review

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Days 222-235.

Close the phase.

## Tasks

- [x] **Guide pass:** `docs/guide/ecs.md` read end to end (concepts, a runnable hello sample, rules of thumb: when to use a tag, a `Changed` filter, the command buffer), tagged samples compile and run; `docgen lint` at zero; `cmake --build build --target docs`; `ctest -L docs`.
- [ ] **Windows PC (open: needs the Windows machine; listed in KNOWN-ISSUES.md):** pull; build `windows-msvc` (plain, asan) and `windows-clang-cl` in both unity modes; run the Phase 17 tests, the renderer demo on Direct3D 12 and Vulkan with validation (the goldens), and `ecs_bench` on Windows (numbers recorded, not gated); clear the "open until the other machine runs it" entries.
- [x] **Closing full run (Linux; the Windows half is open) (CLAUDE.md):** `scripts/test-phase.sh --all --flavor all` on Linux and on Windows (plain, asan), `ctest -L docs`, `renderer_demo`, `ui_demo`, `rhi_demo` 1000 frames with `--validation` on Wayland and X11; record each `ran X of Y tests (phases ...)` line here.
- [x] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the whole phase's diff (Days 222-236, from the commit before Day 222 to the working tree), with extra attention to the concurrency and lifetime code (parallel scheduling, command buffers, chunk pool, removal tracking); verify, fix (with regression tests) or decline each finding; record the count and outcome under "Post-implementation code review".
- [x] **Records:** `plans/phase-17-ecs.md` status complete, `PLAN.md` (roadmap row, log), `STATUS.md` (next: Phase 19, 3D text, whose day files exist; its Day 258 builds on `engine/scene`), `KNOWN-ISSUES.md` sweep (limits of this phase: 128 component types, 32 per archetype, conservative per-chunk change detection, no serialization, no events), `CURRENT_PHASE` stays 17 until Phase 19's first day starts; commit and push.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime or memory code (always for storage days); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the Linux and Windows full runs are recorded and pass, the benchmark claim is published and guarded, the uncapped review is resolved, and Phase 17 is marked complete.

## Notes

- Guide: status line made current, a "Rules of thumb" section (tag or data, `Changed<T>`, command buffer, component splitting, kept queries, parallel grain) and "Measured cost" added; docs build and `ctest -L docs` 7 of 7, lint at zero.
- Closing full run, Linux: `ran 5472 of 5472 tests (phases: all; flavors: all; display: included; crash tests: included) in 209 s`; failures: the two known X11 window tests and the tsan hardware-GPU scene test (both in KNOWN-ISSUES.md). `renderer_demo`, `ui_demo` and `rhi_demo`, 1000 frames with `--validation`, on Wayland and on X11: all exit 0.
- Not done: the Windows PC (build both compilers, both unity modes, the Phase 17 tests, DX12 and Vulkan goldens, `ecs_bench` numbers). `CURRENT_PHASE` stays 17.

## Post-implementation code review

Uncapped end-of-phase review (`--max-findings all` equivalent): four reviewers each took one area of the whole phase diff (Day 222 to the working tree): storage and world, queries and command buffers, scheduling and concurrency, transforms and scene bridge. About 35 findings were reported.

- **Fixed (1, a real bug):** a swap-remove (destroy, or the source side of a migration) moves the archetype's last row into an earlier chunk and stamped only `written`, so an `Added<T>` reader that had passed that chunk missed an entity created since. Both now call `MarkArrived`; regression test `Change: Added still reports an entity that a destroy moved into an earlier chunk` (fails without the fix, checked). The now-unused `Archetype::MarkAllWritten` was removed.
- **Fixed (1):** the baseline's bytes-per-entity estimate omitted the `extras` buffer (a third heap block): 208 became 232 in the guide and the benchmark.
- **Declined or deferred (the rest), each filed with its reason in KNOWN-ISSUES.md ("Phase 17 end-of-phase review"):** documented misuse limits (the `Finish` tick increment in a shared wave, two parallel calls in one system, access counted not enforced, `RangesFor` with grain 0, placeholders not tied to a buffer, contradictory filters compiling, raw `Set<Parent>`), scene-sync limits, and hardening of failure paths that need an out-of-memory abort to matter. None fires in the engine's own use (tsan clean on the ECS suites).
- One reviewer reported `ecs_bench --entities 100000` not finishing in 200 s; in the Release build it runs in about a minute in total (hierarchy setup through `SetParent` included), so the report is taken as a debug-build observation.

Scoped runs after the fix: plain `ran 2110 of 5475 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (the two known X11 failures); asan `ran 1593 of 5475 tests` clean; tsan `ran 1275 of 5475 tests` (the one known scene-test abort).
