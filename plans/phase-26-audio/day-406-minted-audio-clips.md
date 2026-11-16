# Phase 26 / Day 406 — Minted Audio Clips

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 405; Phase 22 (the asset pipeline and `asset_mint`).

Audio clips are assets: minted offline into a layout the mixer reads without conversion, and loaded through the resource manager.

## Tasks

- [ ] A resource type `AudioClip` in `engine/asset/resource_type.h` (the tag is appended, never renumbered) and the blob layout `aclip`: a header (magic, version, rate, layout, sample format, frames, loop start and end), then the samples aligned to 16 bytes; `asset::AudioClipView::Open` validates and refuses a bad blob by value (counts, alignment, sizes checked against the bytes).
- [ ] `asset_mint mint-audio <wav> -o <aclip>` (a command in `tools/asset_mint/`, source file `audio_source.*`): decodes the WAV with Day 405's decoder and writes the blob; options for the stored sample format (`f32` or `s16`) and for normalisation (off by default); deterministic (same inputs and tool version, byte-identical output; a test mints twice and compares).
- [ ] The mint source format (the `.audio` source or a project-file entry, like hulls and meshes): the WAV path, loop points that override the file's, a name; `mint-project` picks it up.
- [ ] The loader: `asset::` resource manager integration so `ResourceManager` loads an `AudioClip` to an `audio::AudioClip` (zero-copy where the blob is aligned: the mixer reads the blob's bytes), reference counted, a missing or corrupt clip a named error and never a silent fallback (the Phase 22 rule: no broken dependencies).
- [ ] Hostile input: `AudioClipView::Open` is fuzzed like the WAV parser; the importer boundary rule holds (`asset_boundary_check`): the runtime never reads a WAV, only the minted blob.
- [ ] Tests: mint then view, the loop points survive, two mints are identical, a corrupted blob at every byte is refused or accepted harmlessly, the resource manager loads and releases a clip.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 428-429, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A WAV is minted deterministically into a clip blob that loads through the resource manager and views without copying.
