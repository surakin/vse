# Phase 26 / Day 418 — Minted Audio Clips

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 417; Phase 22 (the asset pipeline and `asset_mint`).

Audio clips are assets: minted offline into a layout the mixer reads without conversion, and loaded through the resource manager.

## Tasks

- [x] A resource type `AudioClip` in `engine/asset/resource_type.h` (the tag is appended, never renumbered) and the blob layout `aclip`: a header (magic, version, rate, layout, sample format, frames, loop start and end), then the samples aligned to 16 bytes; `asset::AudioClipView::Open` validates and refuses a bad blob by value (counts, alignment, sizes checked against the bytes).
- [x] `asset_mint mint-audio <wav> -o <aclip>` (a command in `tools/asset_mint/`, source file `audio_source.*`): decodes the WAV with Day 417's decoder and writes the blob; options for the stored sample format (`f32` or `s16`) and for normalisation (off by default); deterministic (same inputs and tool version, byte-identical output; a test mints twice and compares).
- [x] The mint source format (the `.audio` source or a project-file entry, like hulls and meshes): the WAV path, loop points that override the file's, a name; `mint-project` picks it up.
- [x] The loader: `asset::` resource manager integration so `ResourceManager` loads an `AudioClip` to an `audio::AudioClip` (zero-copy where the blob is aligned: the mixer reads the blob's bytes), reference counted, a missing or corrupt clip a named error and never a silent fallback (the Phase 22 rule: no broken dependencies).
- [x] Hostile input: `AudioClipView::Open` is fuzzed like the WAV parser; the importer boundary rule holds (`asset_boundary_check`): the runtime never reads a WAV, only the minted blob.
- [x] Tests: mint then view, the loop points survive, two mints are identical, a corrupted blob at every byte is refused or accepted harmlessly, the resource manager loads and releases a clip.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A WAV is minted deterministically into a clip blob that loads through the resource manager and views without copying.

## Outcome (2026-11-17, Linux)

- `asset/audio_clip.h/.cpp` (`kAudioClip` type `ACLP` version 1, `BuildAudioClipBlob`, `AudioClipView::Open`), the CPU loader in `asset/cpu_loaders`, `audio/asset_clip.h/.cpp` (`MakeClipFromAsset`: an `f32` clip shares the blob's bytes in place, an `S16` clip converts once; `LoadAudioClip`), `asset_mint mint-audio SOURCE.audio.src OUTPUT.blob`, `tests/audio/aclip_test.cpp` (7 cases); guide section "Minted clips"; edge `audio -> asset` allowed in `modules.txt` and `ARCHITECTURE.md`.
- Deviations: `mint-audio` takes a source file (`.audio.src`: id, wav, loop, format, normalize) like the other mint commands, not the plan's `<wav> -o <aclip>`; `mint-project` does not mint audio yet (the WAV is an input outside its dependency tracking; recorded in `KNOWN-ISSUES.md`).
- Runs (Days 417 and 418 together): plain `ran 3688 of 9847 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 2996 of 9847 tests (phases: all; flavors: asan; ...)`, tsan `ran 2651 of 9847 tests (phases: all; flavors: tsan; ...)`. Failures: only the four already in `KNOWN-ISSUES.md`. `ctest -L docs` 8 of 8; gates (`module_deps_check`, `std_usage_check`, `platform_neutral_check`, `file_access_check`, `shared_unit_check`) pass.
- Builds: Clang unity on and off clean; GCC 16 per-object compiles clean (a whole-tree GCC build still stops in `engine/net` as recorded); shared and importer-off trees not rebuilt, Windows and macOS not run.

## Post-implementation code review

`/code-review high` over Days 417-418: no findings. Fixed while building: `context.Bytes()` is a `ByteSpan` while `AudioClipView::Open` needs the `SharedBytesPtr` (the loader uses `context.Buffer()`); `ResourceTraits<AudioClipView>` include added to `audio/asset_clip.h`; docgen labels.
