# Phase 26 / Day 430 — Doppler and Distance Filtering

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Days 423, 429.

The two cues that make distance and speed audible beyond loudness.

## Tasks

- [x] Doppler: the pitch ratio from the relative velocity along the line between emitter and listener (`(c - v_listener) / (c + v_emitter)` with the speed of sound and the Doppler factor as settings, clamped to a documented range), velocities from the transform deltas of the ECS sync; the ratio applies to the voice's pitch through the ramp so it never zippers; off per source and by a world factor of 0.
- [x] Velocity sources: the scene transform delta by default, and an explicit velocity (from a physics body, `body_get_velocity` on the C API side) when given; teleports (a jump larger than a bound in one frame) are not velocities and are detected and ignored.
- [x] Air absorption: a one-pole low-pass whose cutoff falls with distance (a table over distance with a documented curve), per voice, using Day 423's filter; the setting is a world factor.
- [x] Both are `+ - * /` on the audio thread; the table is built on the control side with libm (listed).
- [x] Tests: an approaching emitter at a known speed has the pitch ratio of the formula within epsilon; a teleport has no pitch spike; the filter cutoff follows the curve; the doppler of a flyby sweep crosses the unshifted pitch at closest approach; a recorded flyby render hash is committed.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Moving emitters shift in pitch and dull with distance, without artefacts on teleports.

## Outcome (2026-11-17, Linux)

- `DopplerRatio` (`(c + v_l.u)/(c + v_e.u)`, speeds limited to 0.9 c, ratio 0.25-4), applied to the voice pitch by `SpatialAudio` with the transform delta or `AudioVelocity`, teleports (a jump beyond `teleport_distance`) ignored; air absorption: `AirCutoffHz` (20 kHz to 1.2 kHz at 400 m, scaled by the world factor), `AirCoefficient` and the per-voice one-pole low-pass (`Mixer::SetAirAbsorption`). Tests (`doppler_test.cpp`): the formula, an approaching emitter, explicit velocity, a teleport, a flyby crossing 1.0 at closest approach, the cutoff table, the low-pass step response, the committed flyby hash `0xac45a6b75dd064be` (Clang, Linux).
- Runs for Days 427-430 together: plain `ran 3795 of 10162 tests (phases: all; flavors: plain ...)`, asan `ran 3100 of 10162 tests`, tsan `ran 2755 of 10162 tests`; the only failures are the known ones (`engine_c_surface`, two X11Window tests, the jobs frame test and its asan/tsan twins). `ctest -L docs` passes with `docgen lint` at zero; Clang unity on and off and GCC 16 syntax checks of the new objects are clean.

## Post-implementation code review

`/code-review high` over Days 427-430 (read inline), 6 findings reported. Fixed: refused mixer commands were recorded as sent (a regression test fills the queue and fails without the fix); a source switched from spatial to flat stayed in the set and counted twice (regression test); the start-up master volume was never applied (test); `effect_test.cpp` lacked `lang/limits.h` and broke the non-unity build (Day 424's file). Declined with reason (see KNOWN-ISSUES.md): the stereo front-back filter and the ITD (deferred), and `Reconfigure` bypassing inserts on a channel-count change (documented behaviour: the inserts' state is per channel and the application re-creates them).
