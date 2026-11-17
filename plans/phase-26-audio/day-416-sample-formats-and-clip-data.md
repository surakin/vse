# Phase 26 / Day 416 — Sample Formats, Channel Layouts and Clip Data

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 415.

The vocabulary every later day uses: what a sample is, how channels are laid out, and how a decoded clip is held.

## Tasks

- [x] `audio/format.h`: `SampleFormat` (`U8`, `S16`, `S24`, `S32`, `F32`), `ChannelLayout` (mono, stereo, quad, 5.1, 7.1: the channel count and a name per channel), `AudioFormat` (rate, layout, format), the byte sizes, validation (rate 8,000 to 384,000, at most 8 channels).
- [x] `audio/convert.h`: integer to `f32` and back (the scale `1/32768` for 16-bit, the clamp and the dither-free rounding rule on the way back, written once), interleave and deinterleave, channel-count conversion tables (mono to stereo copies, stereo to mono averages with the gain `0.5`, 5.1 to stereo with the documented ITU downmix coefficients), all `+ - * /` only.
- [x] `audio/clip.h`: `AudioClip` (decoded planar or interleaved `f32` frames in a `SharedBytes` with offsets, not pointers; a frame count, the format, loop start and end in frames, a name), its `AudioClipPtr` aliases, `ClipView` over bytes (zero-copy, validated) for a minted blob; `Duration()` helpers; no allocation per voice.
- [x] Memory: a clip is held as `f32` (or `S16` for large clips when the spec chose it) so the mixer converts nothing at play time; the decision and the sizes (a 3-minute stereo 48 kHz clip is 69 MB as `f32`, 35 MB as `S16`) are written down; long clips stream (Day 435).
- [x] Tests: every conversion round-trips within one least significant bit or exactly where it must, the downmix of a known 5.1 frame, the layout channel names, the validation limits.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The formats, the conversions and the clip type exist with tests, and no conversion allocates.

## Outcome (2026-11-17, Linux)

- `audio/format.h`, `audio/convert.h`, `audio/clip.h` (with `AudioClip`, `ClipView`, `MakeClipView`) and their sources; tests `format_test`, `convert_test`, `clip_test` (37 audio test cases in all); `docs/guide/audio.md` section "Formats, conversions and clips".
- Decisions: clips are interleaved `f32` only (planar is a conversion, `S16` storage not chosen: 69 MB per 3-minute stereo 48 kHz clip, streaming from Day 435); `AudioClip` stores a description, a `SharedBytesPtr` and an offset and rebuilds its `ClipView` on demand (no span into another class's storage); a clip refuses NaN/infinity samples, misaligned or short bytes, bad loop points and more than 2^29 frames; creation allocates, `View()` does not.
- Runs: plain `ran 3670 of 9793 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 2978 of 9793 tests (phases: all; flavors: asan; ...)`, tsan `ran 2633 of 9793 tests (phases: all; flavors: tsan; ...)`. Failures: only the four already in `KNOWN-ISSUES.md` (`engine_c_surface`, two X11 window tests, the jobs test, which also fails at HEAD). `ctest -L docs` 8 of 8.
- Builds: Clang unity on (the build tree) and off clean; GCC 16 compiles the audio sources and tests clean in both unity modes (its foundation Release build still stops as recorded on Day 415); shared and importer-off trees were configured on Day 415 and not rebuilt (nothing links `audio` yet).
- Windows and macOS: not run.

## Post-implementation code review

`/code-review high` over the day's diff: no findings. Fixed while reviewing: `ClipView::Frame` now uses `Subspan` so the bounds check holds (it documented an abort it did not have); `format_test.cpp` lacked an include that only unity builds hid (found by the unity-off build); the loop-less `HasLoop` lacked its `Fails:` label (docgen).
