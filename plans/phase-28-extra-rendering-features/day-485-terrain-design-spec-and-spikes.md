# Phase 28 / Day 485 — Terrain Design Spec and Spikes

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 478-484.

Settle the terrain design before any code: data, level of detail, drawing and collision.

## Tasks

- [ ] Write `docs/superpowers/specs/2026-11-NN-terrain-design.md`: the height-map asset, the tile quadtree, the level-of-detail scheme, the draw path, the material model, the collider, the spatial cells, the reset and determinism rules.
- [ ] **Spike 1, the LOD scheme:** a distance-based quadtree of fixed-size grid tiles (CDLOD-style, each tile a 33x33 or 65x65 grid, morphing between levels) against a geometry clipmap; write the cost model (draw count, triangles, crack handling, update cost for a streamed or edited map) and pick; the expected pick is the tile quadtree because it fits the CPU culling decision and the spatial module.
- [ ] **Spike 2, height data on the GPU:** an `R16_UNORM` texture sampled in the vertex shader against a storage buffer of `u16`; check vertex-stage texture fetch on all three backends (Vulkan, D3D12 and Metal all allow it), the precision (16 bits over a user height range), and the bilinear height between samples; pick.
- [ ] **Spike 3, the collider data:** a regular height grid (the same `u16` samples) as the physics shape against a triangle mesh built from it (already supported as `ShapeKind::Mesh`); measure memory and the contact cost of a box and a sphere on 1024x1024 samples for both; the user requires a height-field collider, so the spike decides only its representation (expected: the grid itself, with a per-block min/max pyramid for culling and a fixed diagonal split of each cell, deterministic).
- [ ] **Spike 4, the normals:** derived from the heights at minting (a normal map stored, RG16 octahedral or two-channel) against computed in the shader from four taps; pick (storage against a second set of texture reads per vertex and pixel).
- [ ] Decisions into the spec and `PLAN.md`; adjust days 486-493 if a spike changed their scope.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (a planning and spike day: no `/code-review`; code written for a spike is deleted or kept under `tests/` or `tools/` only if it has a test).

**Done:** The terrain spec is written and committed with the four spike results.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
