# Phase 26 / Day 433 — Compressed Audio II: Packet Decoding and the Inverse Transform

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 432.

Turn audio packets into PCM: floor and residue decode, the inverse MDCT and the overlap-add.

## Tasks

- [ ] Packet decode: mode and window flags, the floor 1 curve (the integer render with the fixed lookup table of 256 entries), the residue decode for types 0, 1 and 2 (partitioned VQ), channel coupling (square polar mapping), the dot product of the floor and the residue.
- [ ] The inverse MDCT for both block sizes (a split-radix or the standard algorithm with precomputed twiddles) and the Vorbis power-complementary windows; the overlap-add between blocks of different sizes; the first packet's discarded output and the last packet's end-trim by the granule position.
- [ ] Tables: the window shapes, the twiddles and the floor lookup are built at decoder creation; where the tables need `sin`/`cos`/`pow` they go through `lang/scalar.h` on the control side (listed); the per-sample path is `+ - * /` and the table lookups; the Day 414 spike decided whether a libm-only difference between machines is accepted with a tolerance and a Linux-only committed hash.
- [ ] The decoder (`audio/vorbis.h`): `VorbisDecoder::Open(bytes)`, `Decode(f32* out, frames)` returning the frames produced, the channel order mapped to the engine's layouts (the Vorbis order for 3 to 8 channels), `Reset`.
- [ ] Hostile input: every index from the stream is range-checked before use, a decode step budget per packet, never an out-of-bounds read; a mutation fuzz that decodes whole streams (not just headers) under asan with a time limit.
- [ ] Tests: the fixtures decode to PCM within the documented tolerance of a reference decode (the test carries a few reference samples and the full decode hash on Linux), the frame count equals the granule position, a mono and a stereo and a coupled stream all pass.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The fixtures decode to PCM of the right length and level, and hostile streams cannot make the decoder misbehave.
