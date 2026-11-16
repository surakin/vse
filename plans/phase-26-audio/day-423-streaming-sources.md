# Phase 26 / Day 423 — Streaming Sources: Long Clips Without Loading Them

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Days 409, 422.

Music and ambience streamed from disk through the file layer, decoded ahead on a job thread, never missing a block.

## Tasks

- [ ] `audio/stream.h`: a `StreamSource` interface (`Read(f32* out, frames) -> frames`, `Seek`, `Length`, `Loop`), with the WAV reader (Day 405), the Vorbis decoder (Day 421) and a memory source behind it; a `StreamVoice` is a mixer voice whose samples come from a ring buffer.
- [ ] The decode-ahead job: a job on the Phase 2 scheduler (not a thread of its own) keeps each stream's ring at least N blocks full (a setting, default 4 blocks of 1,024 frames), reads through `io::File` asynchronously, never touches the mixer's state; the audio thread only reads the ring and, on a shortfall, outputs silence for the missing frames and counts a stream underrun (never waits).
- [ ] Seek and loop on a stream: the command applies at its block stamp, the ring is flushed and refilled from the new position; for a loop the job reads across the wrap so the ring is continuous.
- [ ] Memory: the ring and the decoder state are allocated when the stream is opened; the cost of a stream is documented (a stereo 48 kHz stream with a 4-block ring is about 100 KB plus the decoder's setup).
- [ ] Determinism: the samples a stream produces depend only on the file and the command stamps, never on when the job ran; an underrun is the one non-deterministic event and is reported, and an offline render waits for the job (a mode of the file device) so test hashes are exact.
- [ ] Tests: a long WAV and a long Ogg stream and play to the end with no underrun in the offline mode; a stalled reader produces a counted underrun and recovers; seek during play lands on the right sample; loop is seamless on a ramp; a hundred streams open and close with no growth (asan, leak check).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 428-429, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Long clips stream from disk with decode-ahead on the job system and an exact offline mode.
