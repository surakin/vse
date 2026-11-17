# Phase 26 / Day 420 — The Mixer I: Voices, Resampling, Gain and Pan

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Days 416, 419.

The core loop: fetch samples at a fractional position, scale, place in the stereo field and add.

## Tasks

- [x] `audio/mixer.h`: `Mixer(MixerConfig)` with a fixed block size, a voice pool of `audio.max_voices` slots with generation ids (`VoiceId`), `RenderBlock(out, frames)`; one voice = a clip pointer (`AudioClipPtr`), a 32.32 fixed-point position, a pitch (rate ratio), a gain, a pan, flags.
- [x] The resampler (`audio/resample.h`) at the quality of the Day 414 spike (Day 414: Hermite is the default, `audio.resampler_quality` 0 linear, 1 Hermite, 2 sinc16; the default `audio.max_voices` is 256, the tracked limit 4,096): positions in `u64` 32.32 so a long clip does not drift, interpolation with `+ - * /` only, the loop wrap handled exactly at the loop end (the sample after the end is the one at the loop start, not a mix), pitch changes without clicks.
- [x] The pan law: equal power with `Sqrt` (`left = Sqrt((1 - p) * 0.5)`, `right = Sqrt((1 + p) * 0.5)`, `p` in [-1, 1]), documented with its centre gain (-3 dB); mono clips pan, stereo clips balance, multichannel clips map to the output layout by name.
- [x] Summation in voice-slot order into the output block (never in thread order), the output clipped only at the device boundary (the mixer's own block may exceed 1.0; limiting is Day 422's master bus).
- [x] Voice limit and virtualisation: a voice beyond the limit is dropped by priority (lowest gain, then oldest), a voice marked `virtual` keeps its position advancing and costs no mixing; both deterministic (ties by slot).
- [x] Tests: a known clip at pitch 1 reproduces its samples exactly; at pitch 2 equals every second sample; an octave-down sine keeps its frequency; the loop wrap is seamless on a ramp; pan law gains at -1, 0 and 1; the sum of two voices equals the sum of two renders; the render hash of the reference scene is committed.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A mixer renders voices with resampling, gain and pan, and the reference scene has one committed block hash.

## Outcome (2026-11-17, Linux)

- `audio/mixer.h/.cpp`: `Mixer` (`Init`, `Play`, `Stop`, `SetGain`, `SetPan`, `SetPitch`, `SetVirtual`, `Position`, `RenderBlock`, `Render` with a carry-over), `VoiceId` (slot + generation), `VoiceDesc`, `MixerConfig` (+ `MixerConfigFromSettings`), `MixerStats`; 32.32 positions; linear, 4-point Hermite and 16-tap Blackman-windowed sinc (32 phases, each row normalised, row 0 an exact delta); the loop wrap with wrapped taps; the equal-power pan of a mono clip (centre 0.7071), the balance of a stereo clip, the conversion table for the rest; slot-order summation; stealing by (priority, gain, age); virtual voices. Tests `mixer_test.cpp` (16 cases, the reference-scene hashes committed for the three qualities). Guide section "The mixer".
- Decisions: control calls are direct and must not overlap a render until Day 421's queue; a voice that ends releases its clip on the render thread (Day 421 addresses the last-reference free); the sinc kernel does not low-pass for pitch above 1 (documented); a non-looping voice uses zero beyond the end and holds the first sample before the start.
- Runs (Days 419 and 420 together, before the one-line destructor fix of `WavFileDevice`): plain `ran 3713 of 9918 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 3019 of 9918 tests (phases: all; flavors: asan; ...)`, tsan `ran 2674 of 9918 tests (phases: all; flavors: tsan; ...)`; failures only the four known ones. After the fix: the audio test binaries (plain, asan, tsan) 79 cases pass, `ctest -L phase26` 239 of 239, `ctest -L docs` 8 of 8; gates (`module_deps_check`, `std_usage_check`, `platform_neutral_check`, `file_access_check`, `config_access_check`, `shared_unit_check`, `audio_backend_lists`) pass.
- Builds: Clang unity on and off clean; GCC 16 compiles the new objects clean; GCC cannot link the tests (the `engine/net` failure), so the committed scene hashes were taken with Clang only.
- Regression test for the day's review finding: `Device: a file device destroyed while open still writes its file` (fails without the destructor).

## Post-implementation code review

`/code-review high` over Days 419-420 (read inline): no correctness finding survived except the one noted on Day 419; recorded declines: the sinc is not band-limited for pitch above 1 (documented, Day 442 may revisit), `Render` before `Init` is a misuse (documented).
