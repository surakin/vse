# Phase 26 / Day 418 — Doppler and Distance Filtering

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Days 411, 417.

The two cues that make distance and speed audible beyond loudness.

## Tasks

- [ ] Doppler: the pitch ratio from the relative velocity along the line between emitter and listener (`(c - v_listener) / (c + v_emitter)` with the speed of sound and the Doppler factor as settings, clamped to a documented range), velocities from the transform deltas of the ECS sync; the ratio applies to the voice's pitch through the ramp so it never zippers; off per source and by a world factor of 0.
- [ ] Velocity sources: the scene transform delta by default, and an explicit velocity (from a physics body, `body_get_velocity` on the C API side) when given; teleports (a jump larger than a bound in one frame) are not velocities and are detected and ignored.
- [ ] Air absorption: a one-pole low-pass whose cutoff falls with distance (a table over distance with a documented curve), per voice, using Day 411's filter; the setting is a world factor.
- [ ] Both are `+ - * /` on the audio thread; the table is built on the control side with libm (listed).
- [ ] Tests: an approaching emitter at a known speed has the pitch ratio of the formula within epsilon; a teleport has no pitch spike; the filter cutoff follows the curve; the doppler of a flyby sweep crosses the unshifted pitch at closest approach; a recorded flyby render hash is committed.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 428-429, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Moving emitters shift in pitch and dull with distance, without artefacts on teleports.
