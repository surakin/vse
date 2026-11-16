# Phase 26 / Day 403 — The Audio Module, Determinism Rules and the Block Hash

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 402; Phase 25 Day 374 (the flags and the hash helper).

Create the module and fix the rules that every later day's tests assume.

## Tasks

- [ ] `engine/audio/` with its `CMakeLists.txt`, the `audio` library target, `ARCHITECTURE.md` (layer and allowed edges), `tools/module_deps_check/modules.txt`, `tests/module_map.txt`, `docs/reference/modules.txt` and the root CMake on all three platform paths; `tests/audio/` with its line in `tests/phases.txt`.
- [ ] `audio/block_hash.h`: a 64-bit FNV-1a hash of a block of `f32` samples (bit patterns, little-endian, -0.0 as +0.0, NaN and infinity make the hash invalid) reusing Phase 25's `StateHasher` where it fits; a `RenderHash` that folds blocks in order.
- [ ] Operation rules written into `docs/guide/audio.md` (new page, started here): the mixer uses `+ - * / Sqrt` only, a fixed voice order, a fixed bus order, no reductions in thread order, denormals kept (the same rule as physics, checked by `DenormalsPreserved()`); where libm is unavoidable (filter coefficients, the Doppler ratio's none, decibel conversion) the call goes through `lang/scalar.h` on the control side and is listed in the guide.
- [ ] Reuse the fast-math and contraction configure check of Phase 25 for audio targets (they already apply to every target of ours); a test that a deliberately FMA-friendly gain-and-sum chain gives the same bits under the project's flags.
- [ ] The shared unit list: `tools/shared_unit_check/units.txt` gets `AudioClip` (the decoded samples and their format) with its `AudioClipPtr` and `AudioClipConstPtr` aliases, defined beside the class on Day 404.
- [ ] Tests: the hash on known inputs, the invalid-hash cases, the FMA chain.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 428-429, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The module builds in every configuration, the rules are in the guide, and the block hash has tests.
