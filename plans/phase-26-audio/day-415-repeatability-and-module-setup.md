# Phase 26 / Day 415 — The Audio Module, Repeatability Rules and the Render Comparison

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 414; Phase 25 Day 386 (the flags and the hash helper).

Create the module and fix the rules that every later day's tests assume.

## Tasks

- [ ] `engine/audio/` with its `CMakeLists.txt`, the `audio` library target, `ARCHITECTURE.md` (layer and allowed edges), `tools/module_deps_check/modules.txt`, `tests/module_map.txt`, `docs/reference/modules.txt` and the root CMake on all three platform paths; `tests/audio/` with its line in `tests/phases.txt`.
- [ ] `audio/block_hash.h`: a 64-bit FNV-1a hash of a block of `f32` samples (bit patterns, little-endian, -0.0 as +0.0, NaN and infinity make the hash invalid) reusing Phase 25's `StateHasher` where it fits; a `RenderHash` that folds blocks in order. It is the same-build regression check; across machines scenes compare through `audio/render_compare.h`: per-block RMS and peak and a table of sample probes, equal within the tolerance Day 414 set (max abs error and RMS error in dB reported).
- [ ] Rules written into `docs/guide/audio.md` (new page, started here): the render is repeatable on one build (a fixed voice order, a fixed bus order, no reductions in thread order, block-stamped commands, so thread timing and callback size never change the output); it is **not** promised to be bit-identical across compilers or machines (libm and FMA contraction may differ), and cross-machine checks use the tolerance; the audio thread never allocates, locks or does I/O.
- [ ] No special floating-point flags for audio: the project's defaults apply (Phase 25's flags stay as they are for physics); a test that two renders of the same scene in one process are bit-identical, and that the tolerance comparison reports a deliberately perturbed render (a gain of 1.001) as different.
- [ ] The shared unit list: `tools/shared_unit_check/units.txt` gets `AudioClip` (the decoded samples and their format) with its `AudioClipPtr` and `AudioClipConstPtr` aliases, defined beside the class on Day 416.
- [ ] Tests: the hash on known inputs, the invalid-hash cases, the tolerance comparison on equal, slightly different and very different renders.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The module builds in every configuration, the rules are in the guide, and the block hash and the tolerance comparison have tests.
