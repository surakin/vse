# Phase 26 / Day 404 — Sample Formats, Channel Layouts and Clip Data

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 403.

The vocabulary every later day uses: what a sample is, how channels are laid out, and how a decoded clip is held.

## Tasks

- [ ] `audio/format.h`: `SampleFormat` (`U8`, `S16`, `S24`, `S32`, `F32`), `ChannelLayout` (mono, stereo, quad, 5.1, 7.1: the channel count and a name per channel), `AudioFormat` (rate, layout, format), the byte sizes, validation (rate 8,000 to 384,000, at most 8 channels).
- [ ] `audio/convert.h`: integer to `f32` and back (the scale `1/32768` for 16-bit, the clamp and the dither-free rounding rule on the way back, written once), interleave and deinterleave, channel-count conversion tables (mono to stereo copies, stereo to mono averages with the gain `0.5`, 5.1 to stereo with the documented ITU downmix coefficients), all `+ - * /` only.
- [ ] `audio/clip.h`: `AudioClip` (decoded planar or interleaved `f32` frames in a `SharedBytes` with offsets, not pointers; a frame count, the format, loop start and end in frames, a name), its `AudioClipPtr` aliases, `ClipView` over bytes (zero-copy, validated) for a minted blob; `Duration()` helpers; no allocation per voice.
- [ ] Memory: a clip is held as `f32` (or `S16` for large clips when the spec chose it) so the mixer converts nothing at play time; the decision and the sizes (a 3-minute stereo 48 kHz clip is 69 MB as `f32`, 35 MB as `S16`) are written down; long clips stream (Day 423).
- [ ] Tests: every conversion round-trips within one least significant bit or exactly where it must, the downmix of a known 5.1 frame, the layout channel names, the validation limits.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 428-429, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The formats, the conversions and the clip type exist with tests, and no conversion allocates.
