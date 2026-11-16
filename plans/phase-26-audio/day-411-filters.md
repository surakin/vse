# Phase 26 / Day 411 — Effects I: Filters

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 410.

Low-pass, high-pass and band filters on voices and buses, the building block of occlusion, air absorption and music effects.

## Tasks

- [ ] `audio/filter.h`: a biquad (direct form II transposed, `f32`, state per channel), the types low-pass, high-pass, band-pass, notch, peaking, low and high shelf; coefficients from the cutoff, Q and gain computed on the control side (libm through `lang/scalar.h`, the listed exception) and handed to the audio thread as a plain struct, so the per-sample path is `+ - * /` only.
- [ ] Parameter changes ramp: the coefficients are interpolated across the block (or the cutoff is smoothed on the control side) so a sweep does not click; a filter at the identity setting is bit-transparent (skipped).
- [ ] A one-pole low-pass for cheap per-voice cuts (the occlusion and distance filters of Days 418-419); both are voice inserts and bus inserts through one `Effect` interface (`Process(block)`, `Reset`, `TailFrames`).
- [ ] Denormal handling by the rule of Day 403 (kept); a test that a decaying filter ring-out to silence is exact zero after the documented tail and costs no more than the normal path.
- [ ] Stability: coefficients are checked (poles inside the unit circle), an unstable request is clamped to the nearest stable one and counted.
- [ ] Tests: the magnitude response of each type at five frequencies against the analytic value within a documented tolerance; the impulse response hash (committed; a libm-only difference on another machine is recorded as for physics); the identity filter is transparent; a sweep has no jump above the ramp slope.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 428-429, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Filters work on voices and buses, with the coefficient maths isolated on the control side.
