# Phase 28 / Day 480 — Cube Map Assets: Minting and Loading

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Day 479.

A cube map is a minted asset like any texture: six faces in, one blob out, loaded to a `GpuTexture` with the mip chain.

## Tasks

- [ ] `asset_mint mint-texture` accepts `kind = cube` from six raw RGBA images (the existing `texture_mint` param layout: 1 layer for 2D, 6 for a cube) and from one equirectangular raw image (`kind = cube-from-equirect`, a face size, a deterministic resampling in float with a fixed order of operations); the stored format is the one Day 478 picked (RGBA16F for HDR, RGBA8 for LDR).
- [ ] Minted mips of a cube: a box-filtered chain per face with the edges handled across faces (a seam test: the mean colour of the last mip equals the mean of the faces within a tolerance, and no face edge is darker than its neighbour in the test cube).
- [ ] The texture blob header and `asset::texture.h` already know `Cube` and 6 layers: add the loader path in `asset/gpu_loaders.cpp` that creates a cube `GpuTexture` and a CPU-side `TextureData` the reset path re-uploads (CLAUDE.md: keep the CPU source, register at the tier, never cache a handle).
- [ ] A fallback cube (a 1-pixel-per-face grey cube) next to the existing fallbacks, so a missing cube is never an error mid-frame (`no broken dependencies`: the id is reported, the fallback is drawn).
- [ ] Determinism test: the same six images and parameters mint byte-identical blobs twice and on `--threads 1` and `--threads 8`; a golden hash is committed.
- [ ] Tests: round trip (mint, load, read back each face), a hostile blob (a cube with 5 layers, a non-square face, a size field above the blob) fails by status, a reset of a cube texture (`reset_rig`).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** A cube map is minted from six images or one equirectangular image, deterministically, and loads as a reset-safe `GpuTexture`; the minting guide page lists the new kinds.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
