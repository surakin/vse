# Phase 25 / Day 374 — Determinism Foundation: Flags, Operation Rules and the State Hash

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 373.

Make the build produce the same floating-point results everywhere before a line of physics exists.

## Tasks

- [ ] Compiler flags in `cmake/EngineCompilerOptions.cmake` for every target of ours: `-ffp-contract=off` on GCC and Clang (and `-fno-fast-math` stated), `/fp:precise` (never `/fp:fast`) on MSVC and clang-cl, and the arm64 equivalents; a check in the CMake configure step that fails if a fast-math flag appears in the final compile line; the `build_defs.h` rule for platforms stays.
- [ ] A tiny test target `fp_determinism_probe` that evaluates a fixed expression set (products and sums that FMA would change, a `Sqrt` chain, conversions, denormal inputs) and prints a hash; ctest compares it to a committed constant; the same constant is expected on Windows and macOS x86-64 (checked on Days 398-399).
- [ ] Denormals: the rule of the spec (flush-to-zero is off, or on everywhere, one choice) implemented once in the physics step entry (`PhysicsWorld::Step` sets and restores the mode through an `os/` wrapper if one is needed; add the wrapper to the module of its category first).
- [ ] `engine/physics/state_hash.h`: a 64-bit hash (FNV-1a or the engine's existing one over bytes) of an ordered sequence of `f32`/`u32` words, with a documented byte order; the helper never hashes padding.
- [ ] Operation rules written into `docs/guide/physics.md` (new page, started here): the step uses `+ - * / Sqrt` only, no `Sin`/`Cos`/`Pow`/`Exp` in the step, fixed loop orders, no reductions in thread order; where libm is unavoidable the call goes through `lang/scalar.h` and is listed in the guide.
- [ ] Tests: the probe hash; a test that a deliberately FMA-friendly expression gives the same bits with the flag set (compiled in the test with the project's flags); the hash helper on known inputs.
- [ ] The flag test also covers clang-cl (`-ffp-contract=off` through clang-cl's `/clang:` pass-through, or the MSVC flag set) and states in the probe's output which contraction mode the build has; arm64 is compile-only (clang contracts to FMA by default there, so the flag is what protects it; `KNOWN-ISSUES.md` lists arm64 hashes as unchecked). Day 373 spike 2: hashes differ between GCC and Clang when contraction is on, so the probe must catch it per compiler.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A debug and a release build, GCC and Clang, give the probe's one committed hash, the flags are checked at configure time, and the guide states the rules the rest of the phase obeys.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
