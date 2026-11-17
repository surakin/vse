# Phase 26 / Day 443 — Docs, Closing Runs and the Phase Review

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** All days of Phase 26.

Close the phase on every machine.

## Tasks

- [ ] The guide `docs/guide/audio.md` is complete: concepts, the module and its layer, formats and clips, WAV and Vorbis, the mixer and its block, voices and the command queue, buses and effects, the listener, emitters and spatialisation, Doppler and occlusion, streaming and music, devices and the reset, snapshots, determinism rules and the three-machine result, the C API section, cost and limits; every tagged code sample compiles and runs.
- [ ] The tutorial `docs/tutorials/audio-in-your-window.md` is current; `docs/reference/` is regenerated and committed; `docgen lint` at 100 percent; `ctest -L docs` passes; `BUILD.md` lists any prerequisite the backends added (ALSA headers or none, nothing on Windows and macOS beyond the SDKs).
- [ ] **Closing runs (CLAUDE.md):** `scripts/test-phase.sh --all --flavor all` on Linux, quoting the final `ran X of Y` line; the same on the Windows PC (plain and asan) and the Mac (plain) and `ctest -L docs`; the demos the phase ships (`audio_probe`, `audio_sample`) run; the PC and Mac runs may trail by the rule "no later than the phase's close" and are listed in `KNOWN-ISSUES.md` if they do.
- [ ] Builds clean (warnings are errors) with GCC and Clang, unity on and off, `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF`, on Linux; the closing pass runs the audio tests in the shared configuration too.
- [ ] **Uncapped review (CLAUDE.md):** `/code-review high --max-findings all` over the whole phase's diff (from the commit before Day 414 to the working tree), split by area (formats and decoders, the mixer and effects, the backends, the ECS and spatial integration, snapshots, the C API); verify, fix (a regression test that fails without the fix, shown to fail) or decline each finding with a reason; record the count and outcome under "Post-implementation code review".
- [ ] `STATUS.md`, `PLAN.md` (roadmap row 26 complete, progress log), `KNOWN-ISSUES.md` (every deferred item), `CURRENT_PHASE` stays 26 until the next phase's first day; memory notes updated.
- [ ] Commit and push.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Phase 26 is complete on Linux with the full run quoted, the review recorded, and the PC and Mac runs done or listed.
