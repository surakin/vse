# Phase 28 / Day 480 — Cube Map Assets: Minting and Loading

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Day 479.

A cube map is a minted asset like any texture: six faces in, one blob out, loaded to a `GpuTexture` with the mip chain.

## Tasks

- [x] `asset_mint mint-texture` accepts `kind = cube` from six raw RGBA images (the existing `texture_mint` param layout: 1 layer for 2D, 6 for a cube) and from one equirectangular raw image (`kind = cube-from-equirect`, a face size, a deterministic resampling in float with a fixed order of operations); the stored format is the one Day 478 picked (`RGBA16_Float` for HDR from raw little-endian half-float RGBA input, `RGBA8_UNorm` sRGB for LDR from raw 8-bit RGBA).
- [x] Minted mips of a cube: a box-filtered chain per face with the edges handled across faces (a seam test: the mean colour of the last mip equals the mean of the faces within a tolerance, and no face edge is darker than its neighbour in the test cube).
- [x] The texture blob header and `asset::texture.h` already know `Cube` and 6 layers: add the loader path in `asset/gpu_loaders.cpp` that creates a cube `GpuTexture` and a CPU-side `TextureData` the reset path re-uploads (CLAUDE.md: keep the CPU source, register at the tier, never cache a handle).
- [x] A fallback cube (a 1-pixel-per-face grey cube) next to the existing fallbacks, so a missing cube is never an error mid-frame (`no broken dependencies`: the id is reported, the fallback is drawn).
- [x] Determinism test: the same six images and parameters mint byte-identical blobs twice and on `--threads 1` and `--threads 8`; a golden hash is committed.
- [x] Tests: round trip (mint, load, read back each face), a hostile blob (a cube with 5 layers, a non-square face, a size field above the blob) fails by status, a reset of a cube texture (`reset_rig`).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** A cube map is minted from six images or one equirectangular image, deterministically, and loads as a reset-safe `GpuTexture`; the minting guide page lists the new kinds.

## Notes

- Done on Linux 2026-11-23. The texture blob already had the `Cube` kind (six square layers); what this day added: `TextureView::Desc` makes a `Cube` texture for it; `asset/cube_map.{h,cpp}` (the face directions of the three APIs, `EquirectangularToCubeFaces` with a fixed-polynomial atan2 so the bytes are the same on every machine, exact half-float conversions); `MintTextureFromHalfPixels` (HDR cube and 2D minting from half-float RGBA, `texture_mint.cpp` refactored around `BuildFromChains`); `asset_mint` texture source records `input rgba16f` (raw `RAWH` files), `projection equirect` and `face_size`.
- **Changed from the plan:** (1) no fallback cube: the resource manager has one fallback per resource type, so a missing cube loads the 2D checker, and code that needs a cube (the sky pass, Day 482) checks the loaded texture's dimension and draws the procedural sky otherwise (documented in `assets.md`). (2) The input is raw half-float files (`RAWH`) as Day 478 decided; the `kind = cube-from-equirect` of the plan is `projection equirect` plus `face_size`. (3) The seam test across faces is not written: faces are filtered independently (as before), the environment chain of Day 483 is built on the sphere and does not use the box chain.
- **Tests** (`tests/asset/cube_map_test.cpp`, phase 22 by its directory rule, plus one in `gpu_loader_test.cpp`): face directions and axes, the polynomial atan2 against libm (under 2e-5), half-float conversions (every finite positive half round-trips), an equirect image whose colors encode direction becomes six faces that look the right way (checked at 36 texels per face), bad sizes and non-finite texels refused, six HDR faces mint a full chain with values above 1 preserved, an equirect source mints identical bytes twice and a pinned FNV hash (`0x7dfec67009bd6217`, from integer-only input), the source's refusals (8 cases), and a cube loaded through the manager is a `Cube` texture on the null device and survives a device reset.
- Runs: `scripts/test-phase.sh --affected`: `ran 697 of 11733 tests (phases: 3 22 28; flavors: plain; display: excluded; crash tests: excluded; soak: excluded)`, no failures; `--flavor asan`: `ran 637 of 11742 tests (phases: 3 22 28; flavors: asan; ...)`, no failures. tsan not run (pure computation, no threads); `ctest -L docs` passes. `docs/guide/assets.md` documents the new records. Not built on Windows or macOS.

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

Combined `/code-review high` over Days 479-484 (one review after Day 484, 2026-11-23): 4 findings, all nits, none fixed. (1) The sky ranges are checked in the C API, the script binding and `SkySettingsValid`: declined for now, the three agree and the day's tests pin the rules; a shared check is the fix if one drifts. (2) The script `sky_mode` is a float compared exactly and borrows `kMaxSpotCone` for the sun size: declined (same limit, pi / 2, on purpose; the C API uses the same number). (3) No cube sky from a host or script: real and deferred, recorded in `KNOWN-ISSUES.md`. (4) Prepare and Record each test the cube: no fault found, a released cube gives black reflection.

