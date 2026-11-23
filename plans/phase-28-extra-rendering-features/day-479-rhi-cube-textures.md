# Phase 28 / Day 479 — The RHI Cube Texture (Vulkan and the Null Device)

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Day 478.

Make `rhi::TextureDimension::Cube` real: today it is "reserved, rejected until a backend supports it".

## Tasks

- [ ] `rhi::TextureDesc` validation accepts `Cube` with a square size and exactly 6 layers (or a multiple of 6 for a cube array, rejected for now with a clear status), with mips, and every usage a 2D array allows (sampled, transfer destination, transfer source; storage and render target stay rejected for a cube in this phase and the docs say so).
- [ ] `rhi::Caps` reports `cube_textures` and `cube_array_textures`; the null device reports true (it records only); the validation layer checks the face count and the view range.
- [ ] The Vulkan backend creates the image with the cube-compatible flag (also in the format-properties probe, which passes 0 today) and the default view with `VK_IMAGE_VIEW_TYPE_CUBE`; a texture view of a single face as a 2D view still works (for the minting probe and the prefilter tests); uploads copy per face and per mip through the existing staging path.
- [ ] Shader side: no binding-layout change (a sampled texture names no dimension; the spike confirmed it); `shader_mint` reflection records a texture's dimension and `renderer::shader_reflection` refuses a 2D texture bound where the shader declares a cube (a test).
- [ ] Tests: a cube created, six faces filled with six colours, sampled by direction in a small fragment shader and read back (the `bindings_test.frag` pattern), on the Vulkan backend and the null device; a 5-layer cube and a non-square cube rejected; destroying and recreating after a reset (`tests/rhi` reset rig).
- [ ] The Direct3D 12 and Metal backends return `Unsupported` for a cube until Days 502 and 503 (recorded in `KNOWN-ISSUES.md` as waiting for those days).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** A cube texture is created, filled and sampled by direction on Vulkan and the null device, rejected cleanly elsewhere; the RHI docs and `docs/guide/rhi.md` (or the guide that holds the texture page) describe cubes.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
