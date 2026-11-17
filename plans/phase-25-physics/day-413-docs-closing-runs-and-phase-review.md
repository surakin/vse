# Phase 25 / Day 413 — Docs, Closing Runs and the Phase Review

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** All days of Phase 25.

Close the phase on every machine.

## Tasks

- [x] The guide `docs/guide/physics.md` is complete: concepts, components, the step and its order, shapes and assets, solver tuning, joints, sleeping, CCD, queries, the character controller, events, snapshots, determinism rules and the three-machine result, the C API section, limits; every tagged code sample compiles and runs.
- [x] The tutorial `docs/tutorials/physics-in-your-window.md` is current; `docs/reference/` is regenerated (`cmake --build build --target docs`) and committed; `docgen lint` at 100 %.
- [ ] **Closing runs (CLAUDE.md):** `scripts/test-phase.sh --all --flavor all` on Linux, quoting the final `ran X of Y` line; the same on the Windows PC (plain and asan) and the Mac (plain), and `ctest -L docs`; the demos the phase ships run; the closing runs of the PC and Mac may trail by the rule 'no later than the phase's close' and are listed in `KNOWN-ISSUES.md` if they do.
- [x] Builds clean (warnings are errors) with GCC and Clang, unity on and off, `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF`, on Linux; the closing pass runs the tests in the shared configuration too (the Phase 24 closing left that open).
- [x] **Uncapped review (CLAUDE.md):** `/code-review high --max-findings all` over the whole phase's diff (from the commit before Day 385 to the working tree), split by area if the diff is large; verify, fix (regression test that fails without the fix, shown to fail) or decline each finding with a reason; record the count and outcome under 'Post-implementation code review'.
- [x] `STATUS.md`, `PLAN.md` (roadmap row 25 complete, progress log), `KNOWN-ISSUES.md` (every deferred item), `CURRENT_PHASE` stays 25 until the next phase's first day; memory notes updated.
- [x] Commit and push.

**Done:** Phase 25 is complete on Linux with the full run quoted, the review recorded, and the PC and Mac runs done or listed.

## Outcome

Added the Release-only stress run: `physics_bench --scene large` (ctest `physics_bench_large`, `testopt:` in `tests/phases.txt`, registered for Release/RelWithDebInfo only): 50,000 bodies for 300 steps and 100 bodies for 1,000,000 steps with the state checked as it goes; it passed (194 s). Debug is 6 to 17 times slower than Release on the same scenes (guide, "Limits"). Guide completed (the C API section, the stress run); `docs/reference/` regenerated; `ctest -L docs` 100%.
Closing runs on Linux: the full `scripts/test-phase.sh --all --flavor all`: `ran 9638 of 9638 tests (phases: all; flavors: all; display: included; crash tests: included) in 274 s`, one failure (`test_phase_labels`: the new map line was unused in a Debug build; fixed by making it `testopt:`, rerun passes). After the review fixes: `--affected` plain `ran 3621 of 9653`, asan `ran 2931 of 9653`, tsan `ran 2589 of 9653` (all passed), `ctest -L docs` 8 of 8. Builds: clang unity (build), GCC, no-unity, shared (`ENGINE_BUILD_SHARED=ON`) and Release build clean with warnings as errors; `physics|spatial|ShapeCast|Character` 66 of 66 pass in each of the first four; `ENGINE_LIBRARY_WITH_IMPORTER=OFF` with shared builds, 77 of its tests pass and the three `engine_c_import_shared*` tests (which need the importer) hang and were killed. The PC and Mac closing runs are not done (listed in KNOWN-ISSUES.md).

Closing runs on the Mac (2026-11-17, at Day 426, plain only per the user): four trees built clean (`build-macos`, `build-macos-nounity`, compile-only `build-macos-arm64` and `-arm64-nounity`), `scripts/format.sh --check` clean, `ctest -L docs` 7 of 7, the physics golden and `engine_c_physics_sample` passed; `scripts/test-phase.sh --all --flavor plain`: `ran 3710 of 9851 tests (phases: all; flavors: plain; display: included; crash tests: included) in 568 s`, 2 failed, both Phase 26 audio same-build hashes (`KNOWN-ISSUES.md`, Phase 26 Day 415). The console was locked, so the window tests skipped and are still owed; the Windows PC's runs are still to do.

## Post-implementation code review

Uncapped phase review (CLAUDE.md), done as five area reviews of the whole phase diff (28,751 lines, read-only reviewers: narrow phase and shapes, dynamics, broad phase and queries, snapshots and determinism, C API), about 75 candidates, de-duplicated and checked against the code. Fixed with a regression test that fails without the fix (shown to fail): (1) a collider's group and mask changed through the ECS never reached the body (`SameCollider`); (2) a joint lost because its body was destroyed while the entity lived was reported broken and the `Joint` component removed (`SyncJoints`; confirmed by two reviewers); (3) a character with `skin_width` 0 never found the ground and fell without bound (now refused); (4) a jump against a ceiling kept its upward speed (the character hung there); (5) `ShapeCast` converted a huge distance to an integer (undefined behaviour; a distance beyond 2 * the world bound is now refused; no failing test is possible for the cast itself on x86). The unity build hid a missing include of `physics/check.h`, found by the GCC and no-unity builds. Declined or filed in KNOWN-ISSUES.md with the reason: everything else (see the Day 413 entry there): design limits (queries scan, mesh pair cap of 256 triangles, small-hull EPA thresholds, absolute epsilons for huge bodies, discrete shape cast), determinism-adjacent gaps (the state hash omits joints, cache and contacts; the input recorder covers 7 calls; C API body creation order depends on when a host queries), restore edge cases, and cleanups.
