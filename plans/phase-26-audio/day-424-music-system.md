# Phase 26 / Day 424 — Music: Cross-Fades, Layers, Stingers and Playlists

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 423.

The game-facing behaviour built on streams and buses: what a game does with music.

## Tasks

- [ ] `audio/music.h`: a `MusicPlayer` on the music bus with a current track and a queue; `Play(clip, fade_in)`, `CrossFadeTo(clip, seconds)` (two stream voices with complementary equal-power gains, the old one freed at the end), `Stop(fade_out)`, `Pause`; a transition at a musical boundary when the clip has tempo metadata (beats per minute and beats per bar from the comment tags or the mint source; the switch waits for the next bar boundary computed in frames, exactly).
- [ ] Layers: a track made of N synchronised streams (the same frame position, started at one block stamp) whose gains are controlled separately (the drums fade in with intensity); sync is exact because all layers start on the same block and advance by the same frames; a layer of a different length loops on its own.
- [ ] Stingers: a short clip played over the music at the next beat; ducking of the music bus by dialogue through Day 410's ducker.
- [ ] Playlists: an ordered or shuffled list (the shuffle seeded, deterministic) with a gapless transition for tracks that carry exact sample lengths (the next stream is primed ahead).
- [ ] All timing in frames of the audio clock, so a replay reproduces every transition at the same sample.
- [ ] Tests: a cross-fade sums to constant power at every frame; a bar-aligned transition happens at exactly the computed frame; layers stay in sample sync after a thousand blocks and a seek; a playlist of three clips is gapless (no sample discontinuity on a ramp); a seeded shuffle gives the same order twice.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 428-429, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Music cross-fades, layers, stingers and playlists run on the audio clock with sample-exact timing.
