# Phase 26 / Day 428 — The Listener, Emitters and the ECS Integration

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Days 422, 427; Phase 17 (ECS), Phase 18 (spatial).

Sound that lives in the world: components, a sync system and the spatial query that decides which emitters are worth a voice.

## Tasks

- [x] Components (`audio/components.h`): `AudioSource` (clip, bus, gain, pitch, loop, play-on-start, spatial flag, min and max distance, rolloff model, cone angles and outer gain, priority), `AudioListener` (one active listener; gain; the listener's velocity comes from the transform delta), with validation by value and the `Doppler`/occlusion switches the later days use.
- [x] A sync system over `engine/scene`'s world transforms (the cost is what moved, like the spatial sync): emitter positions and orientations to the mixer's emitter table in a deterministic order (entity order), a changed `AudioSource` updates its voice, a removed entity stops its voice (with the fade).
- [x] Audibility through `engine/spatial` (the rule of PLAN.md assumption #24: no private grid): emitters live in a `SpatialSet` by their audible radius (the distance at which the rolloff reaches the audibility threshold); a listener query finds the emitters in range; those beyond `max_voices` are ranked by loudness (gain over distance, then priority, then entity order) and the rest are virtual.
- [x] Distance models: inverse, linear, exponential and a custom curve (a minted or inline table), clamped to min and max distance; the arithmetic `+ - * /` and `Sqrt` (the exponential through repeated multiplication by an integer exponent, or listed libm use on the control side).
- [x] The listener transform: position and orientation from the ECS camera or an explicit entity, the transform handed to the mixer as plain data each block.
- [x] Tests: an emitter at ten distances has the documented gain per model; a cone attenuates outside its angle; moving only one of 10,000 emitters costs one update; more emitters than voices keep the loudest and the choice is the same on every run; a destroyed entity's voice fades and frees.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Entities with `AudioSource` and `AudioListener` produce spatialised voices chosen by loudness through the spatial module.

## Outcome (2026-11-17, Linux)

- `AudioSource`, `AudioListener` and `AudioVelocity` (`audio/components.h`), the four distance models and the cone (`audio/distance.*`), and `SpatialAudio::Update` (`audio/spatial_audio.*`): the listener, the emitters in a `spatial::SpatialSet` by audible radius, the in-range query, the ranking by loudness then priority then entity, real versus virtual voices, only changes post commands, a destroyed entity fades out. Tests (`spatial_test.cpp`): ten distances per model, the curve, the audible radius, the cone, ranking and ties, 10,000 emitters with one moved (`moved == 1`, no commands), the destroyed entity, the listener velocity, the flat toggle, a refused command resent.
- Deviations: the sync reads `ecs::WorldTransform` (layer 10) rather than `engine/scene` (same layer as audio), so the `audio -> ecs` and `audio -> spatial` edges are new (ARCHITECTURE.md); it scans every source each update instead of a `Changed` filter (the cost that matters, set moves and commands, is per change); a custom curve is an inline table, not a minted one; one-shots start when first audible.
- Runs for Days 427-430 together: plain `ran 3795 of 10162 tests (phases: all; flavors: plain ...)`, asan `ran 3100 of 10162 tests`, tsan `ran 2755 of 10162 tests`; the only failures are the known ones (`engine_c_surface`, two X11Window tests, the jobs frame test and its asan/tsan twins). `ctest -L docs` passes with `docgen lint` at zero; Clang unity on and off and GCC 16 syntax checks of the new objects are clean.

## Post-implementation code review

`/code-review high` over Days 427-430 (read inline), 6 findings reported. Fixed: refused mixer commands were recorded as sent (a regression test fills the queue and fails without the fix); a source switched from spatial to flat stayed in the set and counted twice (regression test); the start-up master volume was never applied (test); `effect_test.cpp` lacked `lang/limits.h` and broke the non-unity build (Day 424's file). Declined with reason (see KNOWN-ISSUES.md): the stereo front-back filter and the ITD (deferred), and `Reconfigure` bypassing inserts on a channel-count change (documented behaviour: the inserts' state is per channel and the application re-creates them).
