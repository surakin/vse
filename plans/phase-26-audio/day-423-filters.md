# Phase 26 / Day 423 — Effects I: Filters

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 422.

Low-pass, high-pass and band filters on voices and buses, the building block of occlusion, air absorption and music effects.

## Tasks

- [x] `audio/filter.h`: a biquad (direct form II transposed, `f32`, state per channel), the types low-pass, high-pass, band-pass, notch, peaking, low and high shelf; coefficients from the cutoff, Q and gain computed on the control side (libm through `lang/scalar.h`, the listed exception) and handed to the audio thread as a plain struct, so the per-sample path is `+ - * /` only.
- [x] Parameter changes ramp: the coefficients are interpolated across the block (or the cutoff is smoothed on the control side) so a sweep does not click; a filter at the identity setting is bit-transparent (skipped).
- [x] A one-pole low-pass for cheap per-voice cuts (the occlusion and distance filters of Days 430-431); both are voice inserts and bus inserts through one `Effect` interface (`Process(block)`, `Reset`, `TailFrames`).
- [x] Denormal handling by the rule of Day 415 (kept); a test that a decaying filter ring-out to silence is exact zero after the documented tail and costs no more than the normal path.
- [x] Stability: coefficients are checked (poles inside the unit circle), an unstable request is clamped to the nearest stable one and counted.
- [x] Tests: the magnitude response of each type at five frequencies against the analytic value within a documented tolerance; the impulse response compared with a committed table within the tolerance; the identity filter is transparent; a sweep has no jump above the ramp slope.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Filters work on voices and buses, with the coefficient maths isolated on the control side.

## Outcome (2026-11-17, Linux)

- `audio/effect.h` (`Effect`: `Process`, `SetParams`, `Reset`, `TailFrames(params)`, `Channels`; a 16-float `EffectParams` block built on the control side), `audio/filter.*` (RBJ biquad in direct form II transposed, seven types, `DesignFilter` with clamping and an unstable-becomes-identity rule, `FilterMagnitude`, a one-pole low-pass), bus inserts in the mixer (`SetInsert`, `ClearInsert`, `SetInsertParams`, four slots per bus, 64 effects per mixer). Tests `effect_test.cpp`. Guide section "Effects".
- Found by tests: a float biquad near the unit circle stalled at a rounding floor around 1e-28, and flushing one state alone restarted the ring-out; both states are now flushed together below 1e-20.
- Deviations: inserts are on buses only (no voice inserts; occlusion filters use a bus of their own); a design with poles outside the circle returns the identity instead of the nearest stable filter.
- Runs (Days 423-426 together): plain `ran 3760 of 10057 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 3065 of 10057 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`, tsan `ran 2720 of 10057 tests (phases: all; flavors: tsan; display: included; crash tests: excluded)`; only the known failures, plus `std_usage_check` (the backend's `<errno.h>` and two `uint32_t`, fixed afterwards: the gates, `audio_tests` in all three flavors, the docs checks and the audio ctests were re-run and pass).

## Post-implementation code review

`/code-review high` over Days 423-426 (see Day 426): the biquad flush fix above.
