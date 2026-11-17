# Phase 26 / Day 421 — Compressed Audio II: Decoding, Channel Order and the Determinism Record

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 420.

Decode the fixtures to PCM through the facade and record exactly how repeatable the output is across builds and machines.

## Tasks

- [ ] Decode every fixture to `f32` through `VorbisDecoder` and compare with a reference decode made when the fixtures were produced (a few reference samples and the full-decode hash committed); the frame count equals the stream's granule length, the first packet's discarded output and the last packet's trim are right, and a mono, a stereo, a coupled and a 5.1 stream all pass.
- [ ] Channel order: Vorbis's order for 3 to 8 channels is mapped to the engine's `ChannelLayout` names and tested with a stream whose channels carry different tones.
- [ ] **The determinism record:** the decode hashes under GCC and Clang, unity and not, Debug and Release (and later Windows and macOS); libvorbis computes its tables and its inverse transform in floating point with libm (`sin`, `cos`, `exp`, `pow`, `atan`), so the same bits across machines are not promised: the day records what was observed, commits the Linux hash, and, if machines differ, the decoded PCM is compared within a documented tolerance and the difference is listed in `KNOWN-ISSUES.md` (the user's rule: libm as is, no reimplemented math). Where a bit-exact result is needed (the mixer's hashes), clips are decoded once at mint time to PCM and the mixer plays the PCM, so the mixer's determinism does not depend on the decoder; streamed music is the case that does, and is hashed per machine.
- [ ] Memory and cost: the decoder's resident size per stream (setup plus buffers) and its cost per second of 44.1 kHz stereo audio, measured and written in the guide ("Cost"); the stream ring size of Day 423 follows from it.
- [ ] Robustness continued: decode of whole mutated streams (not only headers) under asan with a time limit; the facade survives a `Reset` and a re-`Open` of the same object a thousand times with no growth (leak check).
- [ ] Tests: the reference comparison, the channel-order test, the hash table, the soak (open, decode, reset 1,000 times), and the whole-stream fuzz.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 428-429, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The fixtures decode to PCM of the right length, level and channel order, and the cross-build determinism of the decoder is measured and recorded.
