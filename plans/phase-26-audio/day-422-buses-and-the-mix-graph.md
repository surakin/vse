# Phase 26 / Day 422 — Buses: Master, Music, Effects and Sends

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 421.

Group voices, control groups together and put the last gain stage and limiter at the master.

## Tasks

- [ ] `audio/bus.h`: a bus tree (`BusId`; `master` always exists; `music`, `sfx`, `voice` and user buses are created by name), each bus with a gain, a mute, a solo flag and a parent; a voice is assigned to a bus at play time (and can be moved); buses are summed child-before-parent in a fixed order (creation order within a parent) so the sum is deterministic.
- [ ] Sends: a voice or bus can send a fraction of its signal to an auxiliary bus (the future reverb bus of Day 424); a send is a gain and a pre/post-fader flag; the graph is a tree plus sends and a cycle is refused by value.
- [ ] Master bus: a gain, a soft-knee peak limiter (`+ - * / Sqrt` only, a documented attack/release, look-ahead of a fixed number of frames), then a hard clip to [-1, 1] at the device boundary; the limiter's gain reduction is a stat.
- [ ] Ducking: a bus can be ducked by another bus's level (the voice bus ducks the music bus by `x` dB with a release time); level metering (RMS and peak per bus per block) readable from the game thread through the event ring.
- [ ] Decibel helpers (`DbToGain`, `GainToDb`) computed on the control side through `lang/scalar.h` (libm, listed in the guide); the mixer itself takes linear gains.
- [ ] Tests: the master gain scales the render exactly; mute and solo on a known scene; the limiter holds the peak at its threshold on a hot signal and is bit-transparent below it; a cycle is refused; ducking follows a known envelope; the render hash of a three-bus scene is committed.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Voices render through a bus tree with sends, ducking and a limiter, and the mix is deterministic.
