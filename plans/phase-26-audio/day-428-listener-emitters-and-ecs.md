# Phase 26 / Day 428 — The Listener, Emitters and the ECS Integration

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Days 422, 427; Phase 17 (ECS), Phase 18 (spatial).

Sound that lives in the world: components, a sync system and the spatial query that decides which emitters are worth a voice.

## Tasks

- [ ] Components (`audio/components.h`): `AudioSource` (clip, bus, gain, pitch, loop, play-on-start, spatial flag, min and max distance, rolloff model, cone angles and outer gain, priority), `AudioListener` (one active listener; gain; the listener's velocity comes from the transform delta), with validation by value and the `Doppler`/occlusion switches the later days use.
- [ ] A sync system over `engine/scene`'s world transforms (the cost is what moved, like the spatial sync): emitter positions and orientations to the mixer's emitter table in a deterministic order (entity order), a changed `AudioSource` updates its voice, a removed entity stops its voice (with the fade).
- [ ] Audibility through `engine/spatial` (the rule of PLAN.md assumption #24: no private grid): emitters live in a `SpatialSet` by their audible radius (the distance at which the rolloff reaches the audibility threshold); a listener query finds the emitters in range; those beyond `max_voices` are ranked by loudness (gain over distance, then priority, then entity order) and the rest are virtual.
- [ ] Distance models: inverse, linear, exponential and a custom curve (a minted or inline table), clamped to min and max distance; the arithmetic `+ - * /` and `Sqrt` (the exponential through repeated multiplication by an integer exponent, or listed libm use on the control side).
- [ ] The listener transform: position and orientation from the ECS camera or an explicit entity, the transform handed to the mixer as plain data each block.
- [ ] Tests: an emitter at ten distances has the documented gain per model; a cone attenuates outside its angle; moving only one of 10,000 emitters costs one update; more emitters than voices keep the loudest and the choice is the same on every run; a destroyed entity's voice fades and frees.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Entities with `AudioSource` and `AudioListener` produce spatialised voices chosen by loudness through the spatial module.
