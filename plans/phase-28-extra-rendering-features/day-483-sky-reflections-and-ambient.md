# Phase 28 / Day 483 — Reflections and Ambient from the Sky

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 480-482.

Replace the flat sky reflection (`reflection_intensity` against `sky_color`) with the sky itself.

## Tasks

- [ ] Minting: `asset_mint` writes, with every cube texture marked `environment`, a specular chain (the prefilter Day 478 chose, a GGX-style filter at 6 to 9 roughness levels stored as the mips of a second cube or as the lower mips of the same one, whichever the spike recorded) and the 9 spherical-harmonic coefficients of the irradiance in the blob; deterministic, with a fixed sample order; a golden hash.
- [ ] The lighting shader (`lighting_common.slang`) reads the environment cube: reflection = sample at `reflect(view, normal)` and the roughness-selected mip, scaled by `reflection_intensity` and the surface's specular colour; ambient = the SH irradiance at the normal replacing the flat ambient when an environment cube is set; with none, today's flat values apply unchanged (a regression test pins the old images).
- [ ] A procedural sky also feeds them: the sky settings' gradient is turned into the same 9 SH coefficients on the CPU (a function, tested), and the reflection falls back to the gradient evaluated in the shader; so every sky mode has consistent lighting.
- [ ] The lighting pass bind group gains the environment cube and its sampler at a binding that exists in every permutation (a 1x1 cube is bound when none is set, so no pipeline permutation is needed); the push constants stay within 32 bytes.
- [ ] Tests: a smooth mirror sphere under the six-colour cube reflects the right face colours; a rough sphere reflects a blurred, darker-edged version (monotonic variance by roughness); the SH of a uniform cube is that colour and the SH of the procedural gradient matches a brute-force integration within a tolerance; `lighting_test` golden images that used the flat sky are unchanged.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** Lit surfaces reflect and are lit by the sky (cube or procedural), with the flat behaviour preserved when no sky is assigned.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
