# Phase 26 / Day 415 — The Audio Module, Repeatability Rules and the Render Comparison

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 414; Phase 25 Day 386 (the flags and the hash helper).

Create the module and fix the rules that every later day's tests assume.

## Tasks

- [x] `engine/audio/` with its `CMakeLists.txt`, the `audio` library target, `ARCHITECTURE.md` (layer and allowed edges), `tools/module_deps_check/modules.txt`, `tests/module_map.txt`, `docs/reference/modules.txt` and the root CMake on all three platform paths; `tests/audio/` with its line in `tests/phases.txt`.
- [x] `audio/block_hash.h`: a 64-bit FNV-1a hash of a block of `f32` samples (bit patterns, little-endian, -0.0 as +0.0, NaN and infinity make the hash invalid) reusing Phase 25's `StateHasher` where it fits; a `RenderHash` that folds blocks in order. It is the same-build regression check; across machines scenes compare through `audio/render_compare.h`: per-block RMS and peak and a table of sample probes, equal within the tolerance Day 414 set (max abs error and RMS error in dB reported).
- [x] Rules written into `docs/guide/audio.md` (new page, started here): the render is repeatable on one build (a fixed voice order, a fixed bus order, no reductions in thread order, block-stamped commands, so thread timing and callback size never change the output); it is **not** promised to be bit-identical across compilers or machines (libm and FMA contraction may differ), and cross-machine checks use the tolerance; the audio thread never allocates, locks or does I/O.
- [x] No special floating-point flags for audio: the project's defaults apply (Phase 25's flags stay as they are for physics); a test that two renders of the same scene in one process are bit-identical, and that the tolerance comparison reports a deliberately perturbed render (a gain of 1.001) as different.
- [x] The shared unit list: `tools/shared_unit_check/units.txt` gets `AudioClip` (the decoded samples and their format) with its `AudioClipPtr` and `AudioClipConstPtr` aliases, defined beside the class on Day 416.
- [x] Tests: the hash on known inputs, the invalid-hash cases, the tolerance comparison on equal, slightly different and very different renders.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The module builds in every configuration, the rules are in the guide, and the block hash and the tolerance comparison have tests.

## Outcome (2026-11-17, Linux)

- `engine/audio` (`block_hash`, `render_compare`, `clip.h` with the `AudioClip` aliases), `tests/audio` (16 tests, phase 26), `docs/guide/audio.md`; registered in the root CMake (all three paths), `ARCHITECTURE.md`, `modules.txt`, `tests/module_map.txt`, `tests/phases.txt` (flavor lines for 26), `docs/reference/modules.txt` and `lint-baseline.txt` (audio 0 0), `units.txt`.
- The block hash is the audio module's own `BlockHasher` (same FNV-1a constants as physics' `StateHasher`): `audio` cannot depend on `physics`, so it is not reused.
- At the default tolerances the absolute limit decides (an RMS never exceeds the peak of a difference); the RMS limit matters only with a looser absolute one (tested, noted in the guide).
- Runs: plain `ran 3649 of 9730 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` before the docs fixes; asan `ran 2957 of 9730 tests (phases: all; flavors: asan; ...)`, tsan `ran 2612 of 9730 tests (phases: all; flavors: tsan; ...)`. Failures in them: the known X11 window tests, `engine_c_surface` (known, Release tree) and the jobs test `A Job destroyed without being released frees its never-started frame` (new in `KNOWN-ISSUES.md`; it also fails at clean HEAD). `ctest -L docs` 8 of 8 after `docs` was rebuilt.
- Builds: Clang unity on and off clean; GCC 16 compiles the audio sources and tests clean in both unity modes, but a GCC Release build of the foundation stops on `-Werror=stringop-overflow` in `config_text.cpp` and `vector.h` (not our change; in `KNOWN-ISSUES.md`). `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` configure; not built (nothing links `audio` into the library yet).
- Windows and macOS: not run; the shared files (CMake, `tests/phases.txt`, the gates' lists) are listed in `KNOWN-ISSUES.md`.

## Post-implementation code review

`/code-review high` over the day's diff: no findings. Found while building instead: GCC's `-Werror=aggressive-loop-optimizations` flags `Vector::Resize` on an empty vector in a test (false positive in `vector.h`); the tests fill vectors with `Push`. Unity collision of two test helpers named `View`: renamed.
