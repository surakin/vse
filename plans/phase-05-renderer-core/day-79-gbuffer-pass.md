# Phase 5 / Day 79 — The G-buffer Geometry Pass

**Phase:** [Phase 5 — Renderer Core](../phase-05-renderer-core.md)
**Depends on:** Day 78 (mesh vertex colors and topology).

The first real renderer pass: draw every visible object into the G-buffer with its material, depth-tested.

## Tasks

- [x] Create the G-buffer targets as graph transients from the formats fixed in the spec, sized to the swapchain, validated with `FormatSupport` at renderer creation (a missing format is a logged failure naming the format, not a crash).
- [x] Slang geometry shaders: a base `gbuffer.slang` (vertex: object matrix from the object buffer by instance index, world normal; fragment: sample albedo/normal map/material params and pack into the MRTs) with permutations for normal map and alpha test; octahedral normal packing in a shared Slang module with CPU reference functions and a round-trip test.
- [x] Vertex colors in the geometry shader (the mesh side is Day 78): `gbuffer.slang` gets a `VERTEX_COLOR` permutation that multiplies the material albedo by the interpolated vertex color, authored in sRGB and converted to linear in the shader (convention fixed in the spec, with the CPU reference function and a round-trip test). A mesh with colors drawn through a material without the permutation, or the reverse, is a named error at draw-list build time, not a silent mismatch. Test: a two-colored strip of triangles reads back per-vertex gradients from the albedo layer analytically, and a mesh without colors renders exactly as before.
- [x] The pass: cull the scene (Day 73), sort by the Day 74 key, bind pipeline/material groups only on change and issue instanced draws for runs of the same mesh and material; per-draw cost is a handful of RHI calls and no object creation.
- [x] Depth keeps the Phase 4 convention (no reverse-Z): stored `DepthAttachment`, read in the lighting pass as `DepthRead`; near/far derived from the scene bounds by a camera helper.
- [x] Offscreen GPU tests (golden + analytic): in a known scene (a plane, a sphere, a cube behind it) read back the G-buffer layers - albedo equals the material albedo, decoded normals match the geometry within tolerance, the nearer object wins in depth; a shuffled draw order gives identical results; culled objects do not appear.
- [x] A debug view (`ShowGBuffer(layer)`) for the demo: albedo, normals, material, linear depth.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** The pass, the G-buffer layout and the geometry shader contract are documented in the guide and spec. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

## Result

- Shaders: `gbuffer.vert` (permutation `VERTEX_COLOR`), `gbuffer.frag` (permutations `NORMAL_MAP`, `ALPHA_TEST`, `VERTEX_COLOR`, all 8 keys), `gbuffer_debug.frag`, and the shared `gbuffer_common.slang` (octahedral encode/decode, sRGB to linear), all minted; the shader build now depends on `*_common.slang` includes. CPU twins `OctEncode`/`OctDecode`/`SrgbToLinear`/`LinearizeDepth` are tested against known values and a 9,260-direction round trip, and the real-device test decodes the shader's output with the CPU `OctDecode`.
- `engine/renderer/gbuffer.{h,cpp}` in a new library `renderer_passes` (the passes embed the committed shader includes, which `shader_mint` generates and which links the core renderer library; a single library would be a build cycle): formats and `CheckGBufferFormats`, `GBufferPass` (`Prepare` culls, sorts by `MakeDrawSortKey` with the slot as tie-break, writes object data in draw order, merges runs of the same mesh and material into instanced draws; `AddToGraph` declares the four transients and the pass), `CreateGBufferTemplates`/`CreateGBufferMaterial` (pick matched vertex and fragment modules), `GBufferDebugView`, `FitCameraDepthRange`.
- Material system additions: `MaterialAtIndex`, `ReadsVertexInput`, cached `FrameBindGroup`/`ObjectBindGroup` (the pass creates no object per frame), and a vertex input of `Float4` now accepts a `UNorm8x4` attribute (the Day 78 color layout; before this the pipeline for a color shader would have been refused).
- Tests: `tests/renderer/gbuffer_test.cpp` (12 cases: conversions, depth range, material options, pipelines for colored meshes, sorting/instancing counts, every named rejection, bind-group cache, sort ids, location-4 format) and `tests/rhi/vulkan_gbuffer_test.cpp` (5 cases with validation and synchronization validation, zero messages): a known scene read back layer by layer (albedo bytes, normals, material, depth against the projected value, nearer-object-wins, the instanced second cube, the culled object), identical layers for reversed add order, per-vertex gradients, normal map and alpha test, and the debug view of all four layers. Mutations: dropping the sort makes the draw-count checks fail; dropping the cache eviction fails its test.
- Deviations from the task text: (1) the depth target is read by later passes as `Sampled`, not `DepthRead`: `DepthRead` selects the depth-stencil read-only image layout, which validation rejects for a sampled descriptor (found by the debug view); the lighting pass samples depth, so `Sampled` is the right usage there. `DepthRead` stays for a depth test without writes. (2) The mesh must have been prepared (`RecordPrepare`, `MarkReady`) before `Prepare`; an unprepared mesh is a named rejection rather than the pass recording the barriers (the scene holds meshes as `const`). (3) The flags channel carries the material's `flags` parameter; nothing sets bits yet.

## Post-implementation code review

`/code-review high`: 10 findings.

- **Fixed:** the frame/object bind-group cache never evicted, so recreating `FrameResources` (a resize) would fill its 8 slots and fail every frame. The oldest entry is now destroyed through the device's deferred destruction. Regression test (fails with the old behavior).
- **Fixed:** `CreateGBufferTemplates` leaked the plain template when the colored one failed.
- **Fixed:** `GBufferDebugView::Record` recorded the pipeline format as built when creation failed, then aborted at `SetPipeline`. It logs and draws nothing, and retries on the next frame.
- **Fixed:** a vertex color was any attribute at location 4. It is now a `UNorm8x4` attribute there. Test with a `Float4` second uv set.
- **Fixed:** `GpuMesh::SortId` could wrap to 0 (the empty value) and a moved-from mesh kept its id. Ids run 1 to 2^24 - 1 and the source of a move gets 0. Test.
- **Fixed:** `Prepare` allocated a cull list every frame when the scene had more slots than `MaxObjects`; it keeps and grows one member array.
- **Fixed:** the rejection messages used a runtime format string with filler arguments; they go through a `printf`-checked `Reject`, with exact arguments. Dead `Entry::material` removed.
- **Fixed (contract):** a second `AddToGraph` after one `Prepare` overwrote the first call's targets; it now aborts with a message, and the header says one pass object serves one view per frame.
- **Declined:** caching `GetPipeline` per (material, mesh layout) inside `Prepare`. The lookup is a linear scan of the pipeline cache per visible object; recorded in `KNOWN-ISSUES.md` for the Day 83 profiling instead of adding a second cache now.
- **Declined:** keeping every rejection message (only the first is kept, the rest are counted in `rejected`); a per-object list would need storage that grows with the scene.
- **Declined:** a test for the template-leak and debug-pipeline failure paths; both need the device or the shader table to fail on demand, which the null device cannot do.
- **Process:** STATUS.md, KNOWN-ISSUES.md and this file are updated in the same commit.

**Done:** A scene of many materials renders into a G-buffer whose layers are verified analytically and by golden image, with correct depth ordering independent of draw order.
