# Phase 26 / Day 437 — Snapshot, Replay and the Linux Baseline

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Days 422-436.

Make the whole audio state savable and the render provably repeatable on Linux, before the other machines look at it.

## Tasks

- [ ] A snapshot of the mixer and the audio system through `engine/storage` (or the byte encoding of Phase 25 Day 406, reused): voices (clip identity by resource id, position, pitch, gains, ramps, bus), buses, effect states (the delay and reverb buffers, the filter memories), the music player, the command queue's pending commands, the audio clock; a versioned format refused by value when truncated, corrupt or from another version, restore atomic.
- [ ] A recorded input log (the commands with their block stamps, plus the listener and emitter transforms per block) that replays an audio session offline: snapshot at block N, restore and replay the log from N, and the render equals the original from N on.
- [ ] The reference scenes, each with a committed same-build hash (Linux) and a committed reference table (per-block RMS, peak and sample probes) for the cross-machine comparison: a one-voice scene, a thousand-voice scene, buses and a limiter, filters, the reverb, the listener moving past emitters in 5.1, Doppler and occlusion with a test physics world, a cross-fade, a stream (offline mode); every scene's hash and table are committed.
- [ ] The scenes under GCC and Clang, unity and not, Debug and Release, plain, asan and tsan: the same build is bit-identical run to run; different builds agree within the tolerance (the hash is re-committed when a compiler change moves the bits, the table is not).
- [ ] Thread independence: the render is identical whether the commands were posted from one or several threads and whether the device callback ran at block boundaries of 64, 256 or 1,024 frames (the mixer's internal block is fixed, the callback's size is not); the test renders the same scene at three callback sizes.
- [ ] Cost baseline: the cost per voice with and without the resampler quality levels and effects (Release, one thread), recorded in the guide ("Cost"); `audio_bench` is Day 442's.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The audio state snapshots and restores exactly, replays reproduce renders, and every reference scene has a committed hash and comparison table that hold across the Linux builds.
