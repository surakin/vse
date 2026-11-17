# Phase 26 / Day 421 — The Mixer II: Playback Control, Gain Ramps and the Command Queue

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 420.

What a game does with voices (play, stop, pause, seek, change) without clicks and without locks, deterministically.

## Tasks

- [ ] `audio/commands.h`: the command set (`Play`, `Stop`, `Pause`, `Resume`, `Seek`, `SetGain`, `SetPitch`, `SetPan`, `SetLoop`, `Fade`) and a single-producer single-consumer ring (`CommandQueue`) from the game thread to the device thread; each command carries the block number it takes effect at (`now + latency` by default, an explicit block for offline renders); the consumer applies the commands of a block before mixing it, in the order posted.
- [ ] Gain ramps: every gain, pan and pitch change ramps linearly across `ramp_frames` (default 64, a setting) so a parameter step never clicks; a stop fades out over the ramp before the voice is freed; a voice that starts from zero fades in; the ramp arithmetic is `+ - * /` only and exact at the end (the target is reached on the last frame, not approached).
- [ ] `VoiceId` handles: generation-checked, a stale id is a harmless no-op returning a status; `Mixer::Play(clip, params) -> VoiceId` returns at once (the id is allocated on the game thread from a reserved pool so it can be used before the device thread sees the command).
- [ ] Events back to the game thread through a second ring (`VoiceEvent`: finished, looped, virtualised, stolen) read by `PollEvents`; no callbacks cross threads.
- [ ] Overflow: a full command queue returns a status and counts in the stats (never blocks the audio thread, never allocates on it); `MixerStats` (voices playing, virtual, stolen, commands applied, underruns of the queue).
- [ ] Tests: the same command stream posted with different producer timings renders the same bytes (the block-stamp rule); stop and start produce no sample jump larger than the ramp slope; a stale id does nothing; a thousand random commands against a model of the voice state agree; tsan runs the producer and the renderer on two threads.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Voices are controlled through a lock-free queue with click-free ramps, and the render is independent of producer timing.
