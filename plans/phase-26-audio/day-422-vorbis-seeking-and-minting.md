# Phase 26 / Day 422 — Compressed Audio III: Seeking, Loops and Minted Compressed Clips

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Days 406, 421.

Use the decoder where it pays: seek, loop, and keep music compressed in the asset.

## Tasks

- [ ] Seeking: the page index built once on open (granule to byte offset), a bisection seek that lands on a page and decodes the pre-roll blocks needed so the output after the seek is exact (compare with decoding from the start), `SeekFrame(frame)` O(log pages).
- [ ] Loop points for compressed clips: the loop start and end in frames (from the comment tags `LOOPSTART`/`LOOPEND` or the mint source), a seamless wrap by seeking at the loop end and discarding to the start sample, with the pre-roll cost hidden by decoding ahead (Day 423).
- [ ] The minted clip gets a codec field: `aclip` version 1 holds PCM; a new blob kind `aclip` with `codec = vorbis` holds the Ogg bytes verbatim plus the frame count and loop points; `AudioClipView::Open` distinguishes them; `mint-audio` takes an `.ogg` input and a `--keep-compressed` option (the default for clips over a size threshold: music) or decodes to PCM at mint time (short effects); both deterministic (the Ogg bytes are copied as they are; a PCM mint of an Ogg records the decoder's version in the blob header so a decoder change is a visible re-mint).
- [ ] The runtime never reads a loose `.ogg` or `.wav` (the importer boundary rule): only minted blobs; `asset_boundary_check` is extended for the new decoder file names.
- [ ] Tests: a seek to ten random frames equals the same frames decoded from the start; the loop wrap has no discontinuity beyond the codec's own on a sine; a compressed mint is byte-stable; a PCM mint of an Ogg equals decoding the Ogg; the boundary check passes.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 428-429, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Compressed clips seek exactly, loop seamlessly and are minted deterministically.
