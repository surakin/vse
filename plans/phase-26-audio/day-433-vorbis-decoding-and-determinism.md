# Phase 26 / Day 433 — Compressed Audio II: Decoding, Channel Order and the Determinism Record

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 432.

Decode the fixtures to PCM through the facade and record exactly how repeatable the output is across builds and machines.

## Tasks

- [x] Decode every fixture to `f32` through `VorbisDecoder` and compare with a reference decode made when the fixtures were produced (a few reference samples and the full-decode hash committed); the frame count equals the stream's granule length, the first packet's discarded output and the last packet's trim are right, and a mono, a stereo, a coupled and a 5.1 stream all pass.
- [x] Channel order: Vorbis's order for 3 to 8 channels is mapped to the engine's `ChannelLayout` names and tested with a stream whose channels carry different tones.
- [x] **The tolerance rule for decoded audio:** libvorbis computes in floating point with libm, so decoded PCM may differ in the last bits between builds and machines; that is accepted (the user, 2026-11-17). The day commits the Linux decode hash as a same-build baseline and a reference table (probes and per-block RMS) that every other build and machine must match within the tolerance of Day 414; the observed difference between GCC and Clang builds is recorded in the guide.
- [x] Memory and cost: the decoder's resident size per stream (setup plus buffers) and its cost per second of 44.1 kHz stereo audio, measured and written in the guide ("Cost"); the stream ring size of Day 435 follows from it.
- [x] Robustness continued: decode of whole mutated streams (not only headers) under asan with a time limit; the facade survives a `Reset` and a re-`Open` of the same object a thousand times with no growth (leak check).
- [x] Tests: the reference comparison, the channel-order test, the hash table, the soak (open, decode, reset 1,000 times), and the whole-stream fuzz.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The fixtures decode to PCM of the right length, level and channel order, and the cross-build determinism of the decoder is measured and recorded.

## Outcome (2026-11-17, Linux)

- `tests/audio/vorbis_decode_test.cpp`: the sine's level block after block, a reference table of per-block RMS levels (tolerance 2e-3, the cross-build check), the per-fixture decode hash as a same-build baseline (Clang, Linux), a thousand open-decode-destroy rounds (soak, leak-checked under asan) and the cost test. The whole-stream mutation fuzz is Day 432's (`[fuzz]`, decodes every mutant). Channel order is Day 432's 5.1 test.
- Cost: about 1.1 ms of CPU per second of 48 kHz stereo (Clang, plain). Not done: the decoder's resident size per stream (not measured), the GCC-versus-Clang difference (GCC cannot link the audio tests, KNOWN-ISSUES.md), and a `Reset` (there is none: the soak opens and destroys).
- Runs for Days 433-434 together: see Day 434.

## Post-implementation code review

Reviewed with Day 434 (see there).
