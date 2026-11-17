# Phase 26 / Day 427 — Device Reset, Hot-Plug and Switching at Run Time

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 426.

The audio device can be changed, lost and restored without restarting the program (the same principle as the RHI reset).

## Tasks

- [ ] The mixer survives a device change: the render state (voices, positions, buses, effect states) belongs to the `Mixer`, not to the device; closing a device and opening another with a different rate or layout re-plans the output stage (resampler ratios, channel mapping, buffer sizes, effect delay lengths scale with the rate) and the voices go on from where they were.
- [ ] `audio.device`, `audio.sample_rate`, `audio.channels` and `audio.buffer_frames` apply live through one reset function (`AudioSystem::ApplySettings`), classified by cost like `Renderer::ApplySettings` (a volume is free, a rate change replans the effects, a device change reopens).
- [ ] A lost device (unplugged, the server restarted, a sleep) reports through the error callback; the system falls back to the null device so the game keeps running silently and the clock keeps advancing, retries on a timer and on a device-added event, and resumes on the first success; every transition is logged once.
- [ ] Anything that owns device-side or rate-dependent state keeps its source and registers for the reset (the effects keep their parameters, not their buffers' contents of a past rate).
- [ ] Tests with the fake backend: switch rate 44.1 to 48 kHz mid-clip and the clip's pitch and position are right; lose the device, run 100 blocks on the null device, recover, and the voices are where they should be; apply every setting in turn under asan; the device count never grows over 100 resets.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The device can be switched, lost and recovered at run time without losing the mix.
