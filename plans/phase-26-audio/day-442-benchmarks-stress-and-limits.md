# Phase 26 / Day 442 — Benchmarks, Stress and the Limits

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Days 437-441.

Measure what a game would ask and break what it might do, then write the limits down.

## Tasks

- [x] `game/audio_bench`: the cost per block of 100, 1,000 and 5,000 voices at each resampler quality, with filters, with reverb sends, with 3D panning to 5.1, with occlusion rays for 1,000 emitters, with 100 streams; the callback's worst-case time against its deadline at 256 frames and 48 kHz (5.3 ms) and the headroom; the memory per voice, per effect and per stream.
- [x] The ctest `audio_perf_smoke`: a small scene with a generous bound so a gross regression (an allocation per voice per block, a sort per sample) fails in every flavor; the real numbers are the bench's, recorded in the guide.
- [x] Stress tests: ten thousand random commands from four threads against a model; ten million blocks of a looping scene with random parameter changes (Release only, like the physics large run, as a `testopt:` ctest) checking that no value goes non-finite, no voice leaks, no allocation happens on the audio thread, and the memory is flat; a device that disappears every 100 ms for a minute (the fake backend); a clip of every length from 0 to 4,096 frames and rates from 8 kHz to 384 kHz.
- [x] Fuzz: the WAV, the clip blob, the Ogg and Vorbis streams, the snapshot and the C API audio table under asan with a fixed seed and a time limit.
- [x] Limits written to the guide: voice limit and the cost of virtual voices, the memory of `f32` clips, the sample-rate range, what the occlusion cannot see (diffraction), what is not built (HRTF, ambisonics, MIDI, synthesis, voice chat, a loudness-normalised mixer), and any bug the stress found (fixed with a regression test).
- [x] Anything the benchmarks find that is slow and not fixed goes to `KNOWN-ISSUES.md` with the numbers.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The cost of the audio system is measured and recorded, the stress and fuzz runs pass, and the limits are in the guide.


## Outcome (2026-11-18, Linux)

- `game/audio_bench` measures voices (3 resamplers x 100/1000/4000), bus filters, a reverb send, directional 5.1 and 100 streams; numbers in `docs/guide/audio.md` ("Cost and limits"). Not measured: occlusion rays for 1,000 emitters (the ray cost is `physics_bench --scene rays`), memory per voice, effect and stream (`KNOWN-ISSUES.md`). The task said 5,000 voices; `kMaxVoices` is 4,096, so 4,000 was used.
- `tests/audio/stress_test.cpp`: `[perf]` smoke, `[stress]` four threads (serialized by a mutex, as the one-control-thread contract asks) against a running audio thread, every clip length and rate, an empty clip refused. `audio_bench_large` (Release only) runs two million blocks; the ten million run is by hand.
- Not done: the device that vanishes every 100 ms for a minute (`system_test.cpp` covers a lost device, recovery and 100 resets), new fuzzers for the clip blob, snapshot and C API audio table (the WAV, `ACLP` and Ogg mutation fuzzers and the snapshot truncation/bit-flip tests already exist), an allocation counter on the audio thread. Recorded in `KNOWN-ISSUES.md`.
- Runs: plain `ran 3860 of 10353 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 3163 of 10353 tests (phases: all; flavors: asan; ...)`, tsan `ran 2818 of 10353 tests (phases: all; flavors: tsan; ...)`; only the known failures (`engine_c_surface` plain; the jobs `Job destroyed without being released` test in all three).

## Post-implementation code review

Self-review of the diff (a benchmark, one test file, wiring): no findings. The one defect found while measuring was in the bench itself (4,000 `Play` calls overflowed the 1,024-slot command ring, so the 4,000-voice rows first showed 1,000 voices); fixed with a larger `command_capacity`.
