# Phase 26 / Day 427 — Device Reset, Hot-Plug and Switching at Run Time

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 426.

The audio device can be changed, lost and restored without restarting the program (the same principle as the RHI reset).

## Tasks

- [x] The mixer survives a device change: the render state (voices, positions, buses, effect states) belongs to the `Mixer`, not to the device; closing a device and opening another with a different rate or layout re-plans the output stage (resampler ratios, channel mapping, buffer sizes, effect delay lengths scale with the rate) and the voices go on from where they were.
- [x] `audio.device`, `audio.sample_rate`, `audio.channels` and `audio.buffer_frames` apply live through one reset function (`AudioSystem::ApplySettings`), classified by cost like `Renderer::ApplySettings` (a volume is free, a rate change replans the effects, a device change reopens).
- [x] A lost device (unplugged, the server restarted, a sleep) reports through the error callback; the system falls back to the null device so the game keeps running silently and the clock keeps advancing, retries on a timer and on a device-added event, and resumes on the first success; every transition is logged once.
- [x] Anything that owns device-side or rate-dependent state keeps its source and registers for the reset (the effects keep their parameters, not their buffers' contents of a past rate).
- [x] Tests with the fake backend: switch rate 44.1 to 48 kHz mid-clip and the clip's pitch and position are right; lose the device, run 100 blocks on the null device, recover, and the voices are where they should be; apply every setting in turn under asan; the device count never grows over 100 resets.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The device can be switched, lost and recovered at run time without losing the mix.

## Outcome (2026-11-17, Linux)

- `Mixer::Reconfigure(rate, layout)` re-plans voices (step and routing) and keeps positions, buses and the queue; `AudioSystem` (`audio/audio_system.*`) owns the mixer and the device, applies settings by cost (`ApplyCost`: master volume free, rate/channels re-plan and reopen, device/buffer reopen), falls back to the null device on `Lost` or a failed open and retries on a timer, logging each transition once. Tests (`system_test.cpp`, a fake card): 48 kHz to 44.1 kHz mid-clip keeps the place and the pitch, a granted rate that differs is used, loss then 100 blocks of null time then recovery, every setting in turn, 100 resets leave one device alive, the start-up master volume.
- Not done: retry on a device-added event (timer only); effect delay lengths do not rescale by themselves after a rate change (the application rebuilds the parameter blocks, the guide says so); the voice limit and resampler quality need a new mixer and are not applied live.
- Runs for Days 427-430 together: plain `ran 3795 of 10162 tests (phases: all; flavors: plain ...)`, asan `ran 3100 of 10162 tests`, tsan `ran 2755 of 10162 tests`; the only failures are the known ones (`engine_c_surface`, two X11Window tests, the jobs frame test and its asan/tsan twins). `ctest -L docs` passes with `docgen lint` at zero; Clang unity on and off and GCC 16 syntax checks of the new objects are clean.

## Post-implementation code review

`/code-review high` over Days 427-430 (read inline), 6 findings reported. Fixed: refused mixer commands were recorded as sent (a regression test fills the queue and fails without the fix); a source switched from spatial to flat stayed in the set and counted twice (regression test); the start-up master volume was never applied (test); `effect_test.cpp` lacked `lang/limits.h` and broke the non-unity build (Day 424's file). Declined with reason (see KNOWN-ISSUES.md): the stereo front-back filter and the ITD (deferred), and `Reconfigure` bypassing inserts on a channel-count change (documented behaviour: the inserts' state is per channel and the application re-creates them).
