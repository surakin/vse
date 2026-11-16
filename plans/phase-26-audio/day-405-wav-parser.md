# Phase 26 / Day 405 — The WAV Parser

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 404.

Read WAV files the way every game tool writes them, and refuse hostile ones by value.

## Tasks

- [ ] `audio/wav.h`: `ParseWav(bytes) -> WavInfo / status` for RIFF/WAVE with `fmt ` (PCM 8, 16, 24 and 32-bit, IEEE float 32 and 64-bit, `WAVE_FORMAT_EXTENSIBLE` with the channel mask), `data`, `fact`, `smpl` (loop points) and `cue ` (markers) chunks; chunk padding to even sizes; a `data` chunk larger than the file (truncated recordings) accepted up to the bytes present with a warning.
- [ ] A decoder to `AudioClip` (`DecodeWav`) that converts to `f32` frames with the Day 404 conversions; a streaming reader (`WavReader`: open on bytes or an `io::File`, `ReadFrames(count)`, `Seek(frame)`) for Day 423.
- [ ] Hostile input (CLAUDE.md, loaders fail by return value): sizes are checked against what remains before use, no multiplication of untrusted counts without an overflow check, a chunk loop that cannot stop making progress, frame counts capped by a limit, nothing aborts on bad bytes; a fuzz test with a fixed seed that mutates good files and truncates them at every length.
- [ ] Test files are written by the test (a generator that builds WAV bytes for each format), not committed binaries, so the format coverage is in the source.
- [ ] Tests: each format round-trips a known ramp, extensible and plain agree, the loop chunk is read, truncation at every byte never crashes, an oversized chunk is refused, a `data` chunk of an odd length is padded.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 428-429, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** WAV files of every supported format decode to `f32` clips and stream, hostile files are refused by status, and the fuzz test passes under asan.
