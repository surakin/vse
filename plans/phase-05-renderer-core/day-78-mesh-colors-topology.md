# Phase 5 / Day 78 — Mesh Vertex Colors & Primitive Topology

**Phase:** [Phase 5 — Renderer Core](../phase-05-renderer-core.md)
**Depends on:** Day 77.

Two mesh-side capabilities the G-buffer pass and Phase 8's 3D text need: per-vertex colors, and triangle strips (with primitive restart) end to end.

## Tasks

- [x] Per-vertex colors in meshes (needed by Phase 8's colored 3D text, and generally useful for tinted/baked geometry): `MeshData` gains an optional RGBA8 color array (absent means none; `StandardVertex` and every existing mesh and test stay unchanged); `GpuMesh` uploads it as a second vertex stream at a fixed location after the four standard attributes, and `ColorVertexLayout()` describes it (the pipeline cache already keys on the vertex layout). Colors are stored as authored sRGB bytes; the convention is written in the spec and header. The procedural builders get a way to set colors (a per-vertex callback or a constant). Tests: layout/offset/stride checks, upload round trip, validation of a color array whose length differs from the vertex count (named error), and a mesh without colors is byte-identical to before. The geometry shader side is Day 79.
- [x] Triangle strips (and the other topologies) end to end. Today `rhi::PrimitiveTopology::TriangleStrip` exists in `GraphicsPipelineDesc` and maps to Vulkan, but nothing renders with it: no test draws a strip, the RHI has no primitive restart (so one indexed draw cannot hold several strips), and the renderer has no notion of topology. Add: (a) `GraphicsPipelineDesc::primitive_restart` (Vulkan `primitiveRestartEnable`; the restart index is the all-ones value of the index type; validated: only for indexed draws, and only strip topologies unless the device reports list-restart support), documented in the RHI header, with null-device validation; (b) an RHI GPU test with validation and sync validation that draws a quad as a 4-vertex strip, as an indexed strip, and two strips in one indexed draw split by a restart index, reading pixels back (a strip with the wrong winding alternation or a missing restart shows up as a wrong or missing triangle); (c) `MeshData`/`GpuMesh` carry a `PrimitiveTopology` (default `TriangleList`; line and point topologies allowed), the material template's pipeline state takes it from the mesh, and the pipeline cache key includes it, so a strip mesh and a list mesh with the same material get separate pipelines; (d) CPU helpers `StripToList` and `ListToStrip` (degenerate-triangle joins or restart indices, chosen by a flag) with tests that the triangle sets match, and a strip builder for the procedural meshes where it saves vertices (grid, cylinder side); (e) the culling and draw-sort paths handle strips (bounds come from vertices, not triangle counts), pinned by a test.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** The vertex-color stream, `ColorVertexLayout`, `primitive_restart`, the mesh topology, the strip helpers and their conventions are documented in headers and the guide (RHI and renderer pages). `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

## Result

- RHI: `GraphicsPipelineDesc::primitive_restart` (validated for strip topologies only; Vulkan `primitiveRestartEnable`). `tests/rhi/vulkan_strip_test.cpp`: a quad as a 4-vertex strip, as an indexed strip, two strips split by a restart index, and the negative case (restart off draws the marker as a vertex); validation and sync validation silent, pixels read back; the restart test fails if the flag is ignored.
- Renderer: `MeshData::Set` takes a topology (index count must make whole primitives; the all-ones index is a restart marker in a strip and an out-of-range vertex elsewhere; a 16-bit strip addresses at most 65535 vertices), `Topology()`, `HasPrimitiveRestart()`, `PositionOffset()`; `GpuMesh` carries both. `MaterialSystem::GetPipeline`/`Warmup` gained `topology` and `primitive_restart` (defaults keep the old behavior) and the cache key includes them.
- Colors: `ColorVertex` (52 bytes) and `ColorVertexLayout()` (location 4, `UNorm8x4`); `MakeColoredMesh`, `MakeUniformColoredMesh`. **Deviation from the task wording:** the color is interleaved in one vertex buffer (a new layout), not a second vertex stream; a mesh without colors is unchanged byte for byte. One buffer per mesh keeps the draw path and `GpuMesh` as they are, and the RHI stays able to bind several streams if a later need appears.
- Helpers: `MakePlaneStripMesh`, `StripToList`, `ListToStrip` (`StripJoin::Restart`/`Degenerate`). The cylinder-side strip builder was not done (no cylinder builder exists to save vertices on); the plane grid covers the "where it saves indices" requirement.
- Tests: `tests/renderer/mesh_topology_test.cpp` (12 cases: layout, color round trip and errors, topology validation, strip grid versus list grid, degenerate-join removal, list-to-strip round trips for cube/sphere/plane with both joins, shuffled input, culling a strip mesh, position offset, widening), a pipeline-cache case in `material_test.cpp`, restart validation in `types_test.cpp`.
- Docs: lint clean (no new baseline entries), guide section "Vertex colors and primitive topology" with a compiled sample, reference regenerated.

## Post-implementation code review

`/code-review high`: 8 findings.

- **Fixed:** `ListToStrip` with restart joins used `0xFFFF` as the marker when it widened a 65536-vertex 16-bit list to 32-bit indices, so the join became a real triangle. The marker now has the output's width. Regression test (fails without the fix).
- **Fixed:** `StripToList`/`ListToStrip` rebuilt the mesh with position offset 0, so a layout with the position elsewhere got wrong bounds. Added `MeshData::PositionOffset()` and pass it through. Regression test.
- **Fixed:** `MakeUniformColoredMesh` dereferenced a null color. It returns an error string now. Test.
- **Declined:** requiring every sub-strip to hold a primitive. A strip with an empty or short sub-strip draws nothing for it, which is valid GPU behavior; only a strip with no triangle at all is reported by `StripToList`.
- **Declined:** deriving the pipeline's topology and restart from the mesh inside `GetPipeline`. `MaterialSystem` is deliberately mesh-agnostic (it takes a vertex layout the same way); Day 79's G-buffer pass passes the mesh's values, and the RHI validates restart against topology. Revisit if a second caller forgets.
- **Declined:** allowing 65536 vertices in a 16-bit strip without restarts. The limit is stated in the docs and the 32-bit path covers it.
- **Declined:** the O(T²) worst case for non-manifold input in `ListToStrip`. It is an offline/load-time helper; recorded in `KNOWN-ISSUES.md`.
- **Process:** STATUS.md and the day file are updated in this commit.

**Done:** A mesh can carry per-vertex colors and any primitive topology; triangle strips and restart indices render correctly on a real device with validation silent, and strip/list conversions preserve the triangle set.
