# Phase 26 / Day 424 — Effects II: Delay and Reverb

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 423.

Time-based effects with fixed delay lines: echo and a reverb for the aux bus.

## Tasks

- [ ] `audio/delay.h`: a feedback delay with a fixed maximum length (the buffer is allocated when the effect is made, never on the audio thread), a delay time that ramps by interpolated read position, feedback, wet/dry, optional low-pass in the loop.
- [ ] `audio/reverb.h`: a feedback delay network of eight lines with prime-length delays scaled by the rate, a Hadamard feedback matrix (adds and subtracts only), a damping filter per line, pre-delay, size, decay time and wet/dry as parameters; stereo output by decorrelated taps; documented presets (small room, hall, cave).
- [ ] Effect chains: a voice or bus has up to four inserts in a fixed order; an aux bus is typically a reverb at 100 percent wet fed by sends; `TailFrames` lets the mixer keep rendering an effect after its input stops (so a stopped voice's reverb tail is not cut).
- [ ] All arithmetic `+ - * /` only on the audio thread; the control-side maths (decay time to a feedback gain) through `lang/scalar.h` and listed.
- [ ] Tests: an impulse through the delay returns at the set time with the set feedback; the reverb's decay reaches -60 dB within 5 percent of the decay time on an impulse; mono in gives a stereo field; the tail rule keeps the tail; the render hash of an echo scene and a reverb scene are committed.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Delay and reverb run as inserts and on an aux bus with tails and committed hashes.
