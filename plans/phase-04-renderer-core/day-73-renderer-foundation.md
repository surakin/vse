# Phase 4 / Day 73 — Renderer Foundation: Camera, Meshes & the Scene List

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 72.

The data a frame is built from: cameras decoupled from any scene format, GPU meshes, and a flat scene list (the ECS replaces it in Phase 7).

## Tasks

- [ ] `Camera` (look-at or position/orientation, vertical FOV, near/far, aspect; view, projection (`PerspectiveVulkan`), view-projection and inverse matrices; frustum planes), unit-tested numerically; moved from `game/common/cube_scene.*` where that code lives, leaving `rhi_demo` and the RHI tests building.
- [ ] `Mesh` (vertex layout from reflection vertex inputs, interleaved streams, `u16`/`u32` indices, bounding sphere + AABB) and `GpuMesh` (buffers uploaded through the staging ring, destroyed through the device). Procedural meshes: cube, sphere, plane with normals/tangents/uvs, tested for watertightness, CCW winding and unit-length normals.
- [ ] `RenderScene`: a flat array of `RenderObject {mesh, material, transform (Mat4), flags, bounds}` plus lights (declared now, used from Day 79), with stable ids and generation checks like the RHI's handles.
- [ ] Frustum culling (sphere, then AABB) unit-tested including objects straddling planes, behind the camera and at near/far; a timing smoke test for 100k objects recorded, not asserted.
- [ ] Per-frame GPU data: a `FrameData` uniform (view, projection, view-projection, inverse view-projection, camera position, viewport size, time) and one per-object storage buffer (world matrix, normal matrix) rewritten per frame; draws select their object by instance index, so no per-draw bind group is needed (the Phase 3 hotspot).
- [ ] Null-device tests for upload and destruction order; a GPU test that creates, uploads and destroys 1000 meshes with zero leaks (`GetLeakCounts`).
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** Camera, mesh, scene and frame-data types are documented in headers and `docs/guide/renderer.md` gains the concepts and the scene list. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** Meshes, cameras, culling and the scene list work and are tested; per-object data reaches shaders through one buffer indexed per draw.
