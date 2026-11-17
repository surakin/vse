# Phase 26 / Day 429 — Spatialisation: Panning to Stereo and Surround

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 428.

From a direction to speaker gains, for headphones and living rooms.

## Tasks

- [ ] Direction to gains: the emitter's direction in listener space (azimuth and elevation computed with `Sqrt` and divisions from the dot products, no `atan2`) feeds a pan law per layout: stereo (equal power, with a front-back darkening filter from Day 423), quad, 5.1 and 7.1 by vector-based amplitude panning over the layout's speaker directions (the gains from a small linear solve with fixed pivots: `+ - * /` only), normalised for constant power.
- [ ] Spread and focus: a source with a size (a spread angle) lights neighbouring speakers; mono sources centred at zero distance do not jump (the direction is smoothed near the listener).
- [ ] Interaural time difference for stereo headphone mode: an optional delay of up to 0.66 ms on the far ear computed from the azimuth (a fixed-length fractional delay line), off by default; documented as a cheap cue, not HRTF.
- [ ] The low-frequency channel: a send level for the 5.1 and 7.1 LFE (a low-pass of the source above a threshold gain), set by the bus not by position.
- [ ] Gains ramp across the block as in Day 421; a fast pass of an emitter by the listener has no click.
- [ ] Tests: a source straight ahead, left, right and behind gives the documented gains in each layout; the sum of squared gains is 1 within epsilon for 360 azimuths; the gains vary smoothly (no step larger than a bound between adjacent degrees); mono downmix of the stereo output equals the source gain at any azimuth within the law's -3 dB; hashes committed.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Emitters pan correctly in stereo, quad, 5.1 and 7.1 with smooth, constant-power gains.
