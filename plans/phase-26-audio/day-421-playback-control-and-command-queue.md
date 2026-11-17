# Phase 26 / Day 421 — The Mixer II: Playback Control, Gain Ramps and the Command Queue

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 420.

What a game does with voices (play, stop, pause, seek, change) without clicks and without locks, deterministically.

## Tasks

- [x] `audio/commands.h`: the command set (`Play`, `Stop`, `Pause`, `Resume`, `Seek`, `SetGain`, `SetPitch`, `SetPan`, `SetLoop`, `Fade`) and a single-producer single-consumer ring (`CommandQueue`) from the game thread to the device thread; each command carries the block number it takes effect at (`now + latency` by default, an explicit block for offline renders); the consumer applies the commands of a block before mixing it, in the order posted.
- [x] Gain ramps: every gain, pan and pitch change ramps linearly across `ramp_frames` (default 64, a setting) so a parameter step never clicks; a stop fades out over the ramp before the voice is freed; a voice that starts from zero fades in; the ramp arithmetic is `+ - * /` only and exact at the end (the target is reached on the last frame, not approached).
- [x] `VoiceId` handles: generation-checked, a stale id is a harmless no-op returning a status; `Mixer::Play(clip, params) -> VoiceId` returns at once (the id is allocated on the game thread from a reserved pool so it can be used before the device thread sees the command).
- [x] Events back to the game thread through a second ring (`VoiceEvent`: finished, looped, virtualised, stolen) read by `PollEvents`; no callbacks cross threads.
- [x] Overflow: a full command queue returns a status and counts in the stats (never blocks the audio thread, never allocates on it); `MixerStats` (voices playing, virtual, stolen, commands applied, underruns of the queue).
- [x] Tests: the same command stream posted with different producer timings renders the same bytes (the block-stamp rule); stop and start produce no sample jump larger than the ramp slope; a stale id does nothing; a thousand random commands against a model of the voice state agree; tsan runs the producer and the renderer on two threads.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Voices are controlled through a lock-free queue with click-free ramps, and the render is independent of producer timing.

## Outcome (2026-11-17, Linux)

- `audio/commands.h` (`SpscRing`), `Mixer` split into a control side (`Play`, `Stop`, `Pause`, `Resume`, `Seek`, `SetGain/Pan/Pitch` post block-stamped commands; ids and generations reserved on the control side; steals decided there) and an audio side that applies them at the start of `RenderBlock`; events return through a second ring (terminal events free control slots; non-terminal ones are dropped when only the terminal reserve is left); linear per-frame ramps ending exactly on the target (`audio.ramp_frames`, 0 = instant); `SetWatermark`/`TryRenderBlock` for offline use. Tests `control_test.cpp` (13 cases incl. a random 1000-command stream against a model and a producer thread with five timings giving one hash). Guide section "Controlling voices".
- Bug found by a test: the offline watermark started at 0 and let block 0 render before commands were posted (flaky tsan hash); it is stored as block+1 now, with a regression test.
- Runs (Days 421 and 422 together): plain `ran 3737 of 9990 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 3043 of 9990 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`, tsan `ran 2698 of 9990 tests (phases: all; flavors: tsan; display: included; crash tests: excluded)`; failures only the known ones (`engine_c_surface`, two X11Window tests, the Job-destroyed-without-release test and its asan/tsan variants); `ctest -L docs` 8 of 8.

## Post-implementation code review

`/code-review high` over Days 421-422 (read inline). No correctness finding survived beyond the watermark bug above (found by tests). Declined: a voice's last clip reference may be released on the audio thread (documented; the clip is released when its last owner drops it, not an allocation of the mixer).
