# Phase 26 / Day 420 — Compressed Audio I: Ogg and the Vorbis Headers

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Days 404, 405; Day 402 spike 5.

Start the Vorbis decoder with the part that touches untrusted bytes most: the container and the three setup headers.

## Tasks

- [ ] `audio/ogg.h`: the Ogg page parser (capture pattern, version, header type, granule position, serial, sequence, CRC-32 check, the lacing table), packet assembly across pages, multiple logical streams (only the first Vorbis stream is decoded, others skipped), seeking by granule position; every size checked before use, a page loop that always progresses, a limit on the packet size.
- [ ] `audio/vorbis_headers.h`: the identification header (channels, rate, bitrate hints, block sizes), the comment header (tags as strings, never trusted), and the setup header: codebooks (Huffman tree construction with the lookup types 0, 1 and 2, the VQ value tables), time domain transforms, floors (type 1; type 0 is refused with a named status), residues (types 0, 1 and 2), mappings and modes; every count from the stream is bounded by a limit and by the bytes remaining; a malformed Huffman tree (overspecified or underspecified) is refused by value.
- [ ] The codebook memory budget: a decoder's setup is at most a fixed number of bytes; a hostile setup that asks for more is refused.
- [ ] Fuzz tests (a fixed seed): good headers mutated, truncated at every length and bit-flipped; no crash, no unbounded allocation, no hang (a step budget in the test), clean under asan and the sanitizer undefined-behaviour build of the Vorbis files where one exists.
- [ ] Fixtures from Day 402 spike 5: the committed test streams (a few seconds of a sine, noise, a stereo mix, a short and a long block mix) with a README giving the source recipe and licence; their sizes are small.
- [ ] Tests: the pages of the fixtures parse and the CRCs verify; the headers decode to the expected channel count, rate and block sizes; a tag round-trips; every refusal path has a test.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 428-429, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The Ogg container and the Vorbis setup headers parse the fixtures and refuse every malformed input by value.
