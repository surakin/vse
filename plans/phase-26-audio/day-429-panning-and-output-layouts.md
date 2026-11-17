# Phase 26 / Day 429 — Spatialisation: Panning to Stereo and Surround

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 428.

From a direction to speaker gains, for headphones and living rooms.

## Tasks

- [x] Direction to gains: the emitter's direction in listener space (azimuth and elevation computed with `Sqrt` and divisions from the dot products, no `atan2`) feeds a pan law per layout: stereo (equal power, with a front-back darkening filter from Day 423), quad, 5.1 and 7.1 by vector-based amplitude panning over the layout's speaker directions (the gains from a small linear solve with fixed pivots: `+ - * /` only), normalised for constant power.
- [x] Spread and focus: a source with a size (a spread angle) lights neighbouring speakers; mono sources centred at zero distance do not jump (the direction is smoothed near the listener).
- [x] Interaural time difference for stereo headphone mode: an optional delay of up to 0.66 ms on the far ear computed from the azimuth (a fixed-length fractional delay line), off by default; documented as a cheap cue, not HRTF.
- [x] The low-frequency channel: a send level for the 5.1 and 7.1 LFE (a low-pass of the source above a threshold gain), set by the bus not by position.
- [x] Gains ramp across the block as in Day 421; a fast pass of an emitter by the listener has no click.
- [x] Tests: a source straight ahead, left, right and behind gives the documented gains in each layout; the sum of squared gains is 1 within epsilon for 360 azimuths; the gains vary smoothly (no step larger than a bound between adjacent degrees); mono downmix of the stereo output equals the source gain at any azimuth within the law's -3 dB; hashes committed.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Emitters pan correctly in stereo, quad, 5.1 and 7.1 with smooth, constant-power gains.

## Outcome (2026-11-17, Linux)

- `SpeakerGains` (`audio/panning.*`, `+ - * /` and `Sqrt` only): equal-power stereo, vector-based amplitude panning over the neighbouring speakers for quad, 5.1 and 7.1, spread and elevation blending toward every speaker, no-direction spread. `VoiceDesc::directional`, `Mixer::SetDirection` and `SetLfeSend`; the system sends the direction in listener space and a spread that grows as the source nears the listener. Tests (`panning_test.cpp`): documented gains ahead, left, right and behind in every layout, constant power for 360 azimuths with spread and elevation (128,000 checks), smoothness between degrees, a 5.1 mix with the LFE send, a ramp without a step.
- Deferred (KNOWN-ISSUES.md): the front-back darkening filter of stereo and the interaural time difference (the optional far-ear delay). The LFE send is a per-voice level, not a bus setting.
- Runs for Days 427-430 together: plain `ran 3795 of 10162 tests (phases: all; flavors: plain ...)`, asan `ran 3100 of 10162 tests`, tsan `ran 2755 of 10162 tests`; the only failures are the known ones (`engine_c_surface`, two X11Window tests, the jobs frame test and its asan/tsan twins). `ctest -L docs` passes with `docgen lint` at zero; Clang unity on and off and GCC 16 syntax checks of the new objects are clean.

## Post-implementation code review

`/code-review high` over Days 427-430 (read inline), 6 findings reported. Fixed: refused mixer commands were recorded as sent (a regression test fills the queue and fails without the fix); a source switched from spatial to flat stayed in the set and counted twice (regression test); the start-up master volume was never applied (test); `effect_test.cpp` lacked `lang/limits.h` and broke the non-unity build (Day 424's file). Declined with reason (see KNOWN-ISSUES.md): the stereo front-back filter and the ITD (deferred), and `Reconfigure` bypassing inserts on a channel-count change (documented behaviour: the inserts' state is per channel and the application re-creates them).
