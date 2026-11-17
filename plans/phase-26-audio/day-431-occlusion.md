# Phase 26 / Day 431 — Occlusion Through the Physics World

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Days 423, 428; Phase 25 (queries).

Walls stop sound: a cheap, rate-limited ray test that dulls and quiets emitters behind geometry.

## Tasks

- [x] `audio/occlusion.h`: an `OcclusionQuery` interface (`float Occlusion(from, to)` returning 0 to 1 and what the ray crossed); `engine/audio` depends on the interface only.
- [x] The physics implementation (`audio/physics_occlusion.*`, the one place audio names `physics`): casts a ray from emitter to listener through `RayCastAll` with a layer mask, sums the thickness of the colliders it crosses with a per-material absorption (a table on the physics material or a default), and adds a diffraction hint (a second ray offset around the obstacle is not built; the guide says so).
- [x] Rate limiting and smoothing: an emitter is tested at most N times per second (a setting) in a deterministic round-robin by entity order, results are smoothed over time so a door opening fades, and the audible result is a low-pass cutoff and a gain from the occlusion value through Day 423's filter.
- [x] A budget: at most M rays per frame; emitters beyond it keep their last value; the cost is a stat.
- [x] The physics world used is the one the frame steps (Phase 25), read between steps; the queries are the existing `RayCastAll` (Phase 25 Day 403), with the linear scan of its limits noted in the guide (a thousand emitters are fine, a hundred thousand are not).
- [x] Tests: an emitter behind a thick wall has the documented cutoff and gain, behind a thin one less, in the open none; a door that opens fades over the smoothing time; the round-robin visits every emitter in the documented number of frames; the budget is respected; the interface works with a test double (no physics) for the mixer's tests.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Emitters behind geometry are dulled and quieted by rate-limited ray tests against the physics world.

## Outcome (2026-11-17, Linux)

- `audio/occlusion.h` (`OcclusionQuery`, `OcclusionConfig`), the occlusion pass of `SpatialAudio` (a budget of tests per update, round-robin in entity order, per-emitter rate limit, smoothing, `SetOcclusion`, `OcclusionOf`, `occlusion_tests` in the report) and the new module `engine/audio_physics` (layer 14: `PhysicsOcclusion`, rays through the physics world with per-material absorption). Tests: `tests/audio/occlusion_test.cpp` (test double: thick versus thin wall, a door that fades, round-robin coverage, budget) and `tests/audio_physics/physics_occlusion_test.cpp`.
- Deviations: no diffraction ray (the guide says so); the low-pass is the per-voice one-pole of Day 430, not Day 423's bus filter.
- Runs, Days 431-432 together: see Day 432.

## Post-implementation code review

Reviewed together with Day 432 (`/code-review high`, 5 findings in all): the only Day 431 item is the simpler low-pass (declined, above); the module-map line for the vendored directories was fixed.
