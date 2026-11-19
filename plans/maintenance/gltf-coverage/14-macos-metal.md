# glTF coverage / Day 14: the Mac: Metal

**Series:** [glTF coverage](../gltf-coverage.md) (decision 8)
**Depends on:** every shader day (1, 2, 7, 11).

## Tasks

- [ ] **Mint the Metal libraries** of every shader the series changed (`tools/metal_mint`, the committed libraries' macOS build); a CPU block
  must lay out the same in Metal as in std140 (`shader_mint` refuses a difference: the grown `MaterialParams`, `LightData` and the morph
  palette).
- [ ] **Metal bindings:** the new slots, the fourth target, the morph buffers (`engine/rhi/metal/`; argument indices within Metal's limits).
- [ ] **Run** the series' tests on Metal with validation (plain only, the Mac's rule), unity tree; arm64 compile-only.
- [ ] **Look:** the model viewer on Metal.
- [ ] **Standing rows** (on the Mac).

## Notes

(written during the day)

**Done when:** every feature of the series draws on Metal, and no Metal library is stale.
