# Phase 28 / Day 478 — Design Spec, Spikes and Phase Setup (Sky Box)

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Phase 27 complete on Linux.

Start the phase: write the sky box design, settle its open points by spikes, and set up the test phase.

## Tasks

- [ ] `scripts/test-phase.sh --start-day`; `CURRENT_PHASE` becomes 28 in this commit (CLAUDE.md: it changes only in the commit that starts the phase); `tests/phases.txt` gains the label `28` in its header comment and every new test of the phase carries it; `STATUS.md` says the phase started.
- [ ] Write `docs/superpowers/specs/2026-11-NN-sky-box-design.md` (date of the day): the sky as a pass drawn after lighting at depth 1 (a full-screen triangle that writes only where the depth buffer holds the far plane), a procedural sky and a cube-map sky behind one `SkySettings` value, the reflection source, the ambient source, and the reset rules (the cube map is a `GpuTexture` owner with its CPU source kept).
- [ ] **Spike 1, cube textures on the three backends:** read the Vulkan, Direct3D 12 and Metal texture code and write down what a cube needs (Vulkan: `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT` and a `CUBE` view; D3D12: a `TextureCube` shader resource view over a 6-slice array; Metal: `MTLTextureTypeCube`), what `rhi::Caps` must report, how the Slang `TextureCube` binds in the three outputs; record the result and whether any backend needs a workaround.
- [ ] **Spike 2, where the reflection prefilter runs:** at minting (deterministic, bytes in the asset, no run-time cost, the rule of `PLAN.md` "minting must be deterministic") against on the GPU at load (a compute pass, no bigger assets). Measure the minted size of a 512x512 RGBA16F cube with a 9-level specular chain, and the time to prefilter it on one CPU thread. Record the pick (expected: minting, with a GPU path left out).
- [ ] **Spike 3, the ambient term:** compare a 9-coefficient spherical-harmonic irradiance computed at minting (stored in the asset) with the flat ambient colour in use today, on the reference sphere scene; record whether it is part of this phase (expected: yes, 36 floats in the cube asset).
- [ ] **Spike 4, the HDR format:** RGBA16F against RGB9E5 against RGBA8 with a shared exponent for the source; pick the stored format (all three backends can sample RGBA16F) and the minting input (six 8-bit PNG-free raw images, or an equirectangular `.hdr`-free raw float image; the engine reads no PNG or HDR at run time).
- [ ] Decisions recorded in the spec and `PLAN.md` (an assumption entry for the sky and the cube texture), and the day files 479-484 adjusted if a spike changed their scope.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (a planning and spike day: no `/code-review`; code written for a spike is deleted or kept under `tests/` or `tools/` only if it has a test).

**Done:** The spec is written and committed, the four spikes have recorded results, `CURRENT_PHASE` is 28 and the phase label is in `tests/phases.txt`.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
