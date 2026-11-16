# Phase 26 / Day 419 — Occlusion Through the Physics World

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Days 411, 416; Phase 25 (queries).

Walls stop sound: a cheap, rate-limited ray test that dulls and quiets emitters behind geometry.

## Tasks

- [ ] `audio/occlusion.h`: an `OcclusionQuery` interface (`float Occlusion(from, to)` returning 0 to 1 and what the ray crossed); `engine/audio` depends on the interface only.
- [ ] The physics implementation (`audio/physics_occlusion.*`, the one place audio names `physics`): casts a ray from emitter to listener through `RayCastAll` with a layer mask, sums the thickness of the colliders it crosses with a per-material absorption (a table on the physics material or a default), and adds a diffraction hint (a second ray offset around the obstacle is not built; the guide says so).
- [ ] Rate limiting and smoothing: an emitter is tested at most N times per second (a setting) in a deterministic round-robin by entity order, results are smoothed over time so a door opening fades, and the audible result is a low-pass cutoff and a gain from the occlusion value through Day 411's filter.
- [ ] A budget: at most M rays per frame; emitters beyond it keep their last value; the cost is a stat.
- [ ] The physics world used is the one the frame steps (Phase 25), read between steps; the queries are the existing `RayCastAll` (Phase 25 Day 391), with the linear scan of its limits noted in the guide (a thousand emitters are fine, a hundred thousand are not).
- [ ] Tests: an emitter behind a thick wall has the documented cutoff and gain, behind a thin one less, in the open none; a door that opens fades over the smoothing time; the round-robin visits every emitter in the documented number of frames; the budget is respected; the interface works with a test double (no physics) for the mixer's tests.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 428-429, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Emitters behind geometry are dulled and quieted by rate-limited ray tests against the physics world.
