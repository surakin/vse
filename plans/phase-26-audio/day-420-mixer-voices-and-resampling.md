# Phase 26 / Day 420 — The Mixer I: Voices, Resampling, Gain and Pan

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Days 416, 419.

The core loop: fetch samples at a fractional position, scale, place in the stereo field and add.

## Tasks

- [ ] `audio/mixer.h`: `Mixer(MixerConfig)` with a fixed block size, a voice pool of `audio.max_voices` slots with generation ids (`VoiceId`), `RenderBlock(out, frames)`; one voice = a clip pointer (`AudioClipPtr`), a 32.32 fixed-point position, a pitch (rate ratio), a gain, a pan, flags.
- [ ] The resampler (`audio/resample.h`) at the quality of the Day 414 spike (Day 414: Hermite is the default, `audio.resampler_quality` 0 linear, 1 Hermite, 2 sinc16; the default `audio.max_voices` is 256, the tracked limit 4,096): positions in `u64` 32.32 so a long clip does not drift, interpolation with `+ - * /` only, the loop wrap handled exactly at the loop end (the sample after the end is the one at the loop start, not a mix), pitch changes without clicks.
- [ ] The pan law: equal power with `Sqrt` (`left = Sqrt((1 - p) * 0.5)`, `right = Sqrt((1 + p) * 0.5)`, `p` in [-1, 1]), documented with its centre gain (-3 dB); mono clips pan, stereo clips balance, multichannel clips map to the output layout by name.
- [ ] Summation in voice-slot order into the output block (never in thread order), the output clipped only at the device boundary (the mixer's own block may exceed 1.0; limiting is Day 422's master bus).
- [ ] Voice limit and virtualisation: a voice beyond the limit is dropped by priority (lowest gain, then oldest), a voice marked `virtual` keeps its position advancing and costs no mixing; both deterministic (ties by slot).
- [ ] Tests: a known clip at pitch 1 reproduces its samples exactly; at pitch 2 equals every second sample; an octave-down sine keeps its frequency; the loop wrap is seamless on a ramp; pan law gains at -1, 0 and 1; the sum of two voices equals the sum of two renders; the render hash of the reference scene is committed.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A mixer renders voices with resampling, gain and pan, and the reference scene has one committed block hash.
