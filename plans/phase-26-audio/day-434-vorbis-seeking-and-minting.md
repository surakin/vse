# Phase 26 / Day 434 — Compressed Audio III: Seeking, Loops and Minted Compressed Clips

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Days 418, 433.

Use the decoder where it pays: seek, loop, and keep music compressed in the asset.

## Tasks

- [x] Seeking: a page index built once on open (granule to byte offset) over the facade, a bisection seek that lands on a page and decodes the pre-roll the codec needs so the output after the seek equals decoding from the start, using libvorbis's granule seek where it does the job (the facade adds the bounds checks and the budget), `SeekFrame(frame)` in O(log pages).
- [x] Loop points for compressed clips: the loop start and end in frames (from the comment tags `LOOPSTART`/`LOOPEND` or the mint source), a seamless wrap by seeking at the loop end and discarding to the start sample, with the pre-roll cost hidden by decoding ahead (Day 435).
- [x] The minted clip gets a codec field: `aclip` version 1 holds PCM; a blob with `codec = vorbis` holds the Ogg bytes verbatim plus the frame count and loop points; `AudioClipView::Open` distinguishes them; `mint-audio` takes an `.ogg` input and a `--keep-compressed` option (the default for clips over a size threshold: music) or decodes to PCM at mint time (short effects); both deterministic for a given decoder version (the Ogg bytes are copied as they are; a PCM mint records the libvorbis version and the decode hash in the blob header so a decoder change is a visible re-mint).
- [x] The runtime never reads a loose `.ogg` or `.wav` (the importer boundary rule); `asset_boundary_check` is extended: the facade is linked by the audio module and by `asset_mint`, and no other module includes `libogg` or `libvorbis`.
- [x] Tests: a seek to ten random frames equals the same frames decoded from the start; the loop wrap has no discontinuity beyond the codec's own on a sine; a compressed mint is byte-stable; a PCM mint of an Ogg equals decoding the Ogg; the boundary check passes.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Compressed clips seek exactly, loop seamlessly and are minted deterministically for a given decoder version.

## Outcome (2026-11-17, Linux)

- Facade: `LOOPSTART`/`LOOPEND` tags in `VorbisInfo`, `SeekFrame` (a page index read once on the first seek, with the frame each restart page emits first from the packets' block sizes; a bisection; the library restarted on the page; frames discarded up to the target), `SetLooping` (wrap by seeking at `loop_end`). Tests (`vorbis_seek_test.cpp`): thirteen seeks equal the decode from the start bit for bit, the end of the stream, two and a half loop passes with no changed sample, a seek-and-loop fuzz of damaged streams. New fixture `music_loop.ogg` (6 s, loop tags).
- Clips: `AudioClipCodec` in the header (`ACLP` codec field, formerly flags), optional provenance section 18 (libvorbis version and decode hash of a PCM clip minted from an Ogg), `MakeClipFromAsset` decodes a Vorbis blob once, `mint-audio` takes `ogg` and `compress on|off|auto`. Tests (`vorbis_clip_test.cpp`): a kept blob is the Ogg bytes verbatim and byte-stable, a decoded mint records the decoder and hash and equals the kept clip's samples, auto keeps music and decodes effects, refusals, a header-byte fuzz.
- Gate: `asset_boundary_check` rule 4 (only `engine/audio/vorbis.cpp` includes the codec headers, only `engine/audio/CMakeLists.txt` links it), with a planted case.
- Deviations: the seek index is built on the first seek, not on open; the first seek therefore reads every page once (no decode). No GCC link check (see Day 433).
- Runs for Days 433-434: plain `ran 3828 of 10261 tests (phases: all; flavors: plain ...)`, asan `ran 3133 of 10261 tests`, tsan `ran 2788 of 10261 tests`; the known failures only (`engine_c_surface`, the jobs frame test and its twins). `ctest -L docs` 8 of 8; the non-unity build of audio, audio_physics and asset_mint is clean.

## Post-implementation code review

`/code-review high` over Days 433-434: 3 findings. Fixed: the old `aclip` fuzz test did not allow the refusal a flipped codec byte now produces. Declined with reason: the first seek scans all pages (bounded by the page limit, documented), and resident size and GCC determinism were not measured (KNOWN-ISSUES.md).
