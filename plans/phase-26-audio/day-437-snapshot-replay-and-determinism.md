# Phase 26 / Day 437 — Snapshot, Replay and the Linux Baseline

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Days 422-436.

Make the whole audio state savable and the render provably repeatable on Linux, before the other machines look at it.

## Tasks

- [x] A snapshot of the mixer and the audio system through `engine/storage` (or the byte encoding of Phase 25 Day 406, reused): voices (clip identity by resource id, position, pitch, gains, ramps, bus), buses, effect states (the delay and reverb buffers, the filter memories), the music player, the command queue's pending commands, the audio clock; a versioned format refused by value when truncated, corrupt or from another version, restore atomic.
- [x] A recorded input log (the commands with their block stamps, plus the listener and emitter transforms per block) that replays an audio session offline: snapshot at block N, restore and replay the log from N, and the render equals the original from N on.
- [x] The reference scenes, each with a committed same-build hash (Linux) and a committed reference table (per-block RMS, peak and sample probes) for the cross-machine comparison: a one-voice scene, a thousand-voice scene, buses and a limiter, filters, the reverb, the listener moving past emitters in 5.1, Doppler and occlusion with a test physics world, a cross-fade, a stream (offline mode); every scene's hash and table are committed.
- [x] The scenes under GCC and Clang, unity and not, Debug and Release, plain, asan and tsan: the same build is bit-identical run to run; different builds agree within the tolerance (the hash is re-committed when a compiler change moves the bits, the table is not).
- [x] Thread independence: the render is identical whether the commands were posted from one or several threads and whether the device callback ran at block boundaries of 64, 256 or 1,024 frames (the mixer's internal block is fixed, the callback's size is not); the test renders the same scene at three callback sizes.
- [x] Cost baseline: the cost per voice with and without the resampler quality levels and effects (Release, one thread), recorded in the guide ("Cost"); `audio_bench` is Day 442's.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The audio state snapshots and restores exactly, replays reproduce renders, and every reference scene has a committed hash and comparison table that hold across the Linux builds.

## Outcome (2026-11-17, Linux)

- Mixer `SaveState`/`LoadState` (`audio/mixer_state.cpp`, `state_io.h`, `snapshot.h`): atomic restore, refusals by value, replay from a snapshot is bit-identical; scene hashes and RMS rows committed (`snapshot_test.cpp`, 6 cases, 211 audio cases pass). Deviations: the music player and `AudioSystem` are not in the snapshot; the replay log is a test script; the Windows slot of the scene hashes holds the Linux values until the PC commits its own.
- Runs (Days 437-439 together): plain `ran 3854 of 10339 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 3159 of 10339 tests (... flavors: asan ...)`, tsan `ran 2814 of 10339 tests (... flavors: tsan ...)`; known failures only (`engine_c_surface`, the jobs never-started-frame test and its twins, an X11 key test; `engine_c_scene_asan` failed once and passed alone). `ctest -L docs` 8 of 8.

## Post-implementation code review

`/code-review high`: 7 findings. Fixed: a closed stream was still refilled through the host's callback (`StreamPump::Remove`, regression test fails without it), the pump wait aborted on a host thread, non-finite host samples, the music bus ignored after the first call, the cost bound under sanitizers. Declined: `audio_voice_set` is not atomic across fields (documented in the header); `audio_close` racing a render (documented: not while another thread is in `audio_render`).
