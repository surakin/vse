# glTF coverage / Day 7: morph targets on the GPU

**Series:** [glTF coverage](../gltf-coverage.md) (decision 4)
**Depends on:** Day 6.

## Tasks

- [ ] **The owner** (`renderer/mesh.*`, `asset/gpu_loaders.cpp`): `GpuMesh` uploads the `MORP` deltas into a storage buffer (dense per
  target is simpler for the shader: decide between dense `target * vertex_count + vertex` and the sparse list with a per-target vertex
  map, measure the memory of a 52-target face, write it down); it is part of the mesh's device objects, released and recreated with them
  from the kept `MeshData` (the reset rule; no new shared unit unless the buffer is shared by more than the mesh).
- [ ] **The object** (`renderer/render_scene.*`): `SetObjectMorphWeights(ObjectId, Span<const f32>)` (at most 64, the mesh's count);
  per frame `FrameResources` writes each morphed object's active list (the 8 largest `|weight|`, ties by target index; zero weights
  skipped; the rest counted in a stat) into a morph palette buffer, and the object's offset into it next to the skin palette offset (the
  `ObjectData` layout is documented in `frame_data.h`: change it there and in every shader that reads it).
- [ ] **Shader** (`gbuffer.vert`): option `MORPHED` (the keys grow from 4 to 8): before skinning, `position += sum w_i * dp_i`, the same
  for the normal and the tangent's xyz, the vertex found by `SV_VertexID`; a new set (or binding in set 3) for the deltas and the palette:
  pick the register and space so the DXIL and Metal rules hold (`shaders/README.md`, `tests/shader_mint/d3d12_conventions_test.cpp`).
- [ ] **The pipeline:** `gbuffer` templates pick the `MORPHED` key when the mesh has targets; culling uses the grown bounds (Day 6).
- [ ] **CPU reference:** the morphed position of a vertex on the CPU; a test compares a readback.
- [ ] **Tests:** a quad with two targets at weights (0, 0), (1, 0), (0.5, 0.5), (-1, 0) draws where the CPU says; 9 non-zero weights use the
  8 largest (and the stat counts one); a morphed and skinned mesh (the order); the reset rig (counts and memory back); the sanitizer flavors.
- [ ] **Mint** SPIR-V of `gbuffer.vert` (and any shader reading `ObjectData`); DXIL and Metal wait.
- [ ] **Standing rows.**

## Notes

(written during the day)

**Done when:** a mesh with morph targets draws its weighted shape on Vulkan, with skinning after the morph, and survives a device reset.
