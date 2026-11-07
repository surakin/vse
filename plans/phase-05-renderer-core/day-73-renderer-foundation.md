# Phase 5 / Day 73 — Renderer Foundation: Camera, Meshes & the Scene List

**Phase:** [Phase 5 — Renderer Core](../phase-05-renderer-core.md)
**Depends on:** Day 72.

The data a frame is built from: cameras decoupled from any scene format, GPU meshes, and a flat scene list (the ECS replaces it in Phase 14).

## Tasks

- [x] `Camera` (look-at or position/orientation, vertical FOV, near/far, aspect; view, projection (`PerspectiveVulkan`), view-projection and inverse matrices; frustum planes), unit-tested numerically; moved from `game/common/cube_scene.*` where that code lives, leaving `rhi_demo` and the RHI tests building.
- [x] `Mesh` (vertex layout from reflection vertex inputs, interleaved streams, `u16`/`u32` indices, bounding sphere + AABB) and `GpuMesh` (buffers uploaded through the staging ring, destroyed through the device). Procedural meshes: cube, sphere, plane with normals/tangents/uvs, tested for watertightness, CCW winding and unit-length normals.
- [x] `RenderScene`: a flat array of `RenderObject {mesh, material, transform (Mat4), flags, bounds}` plus lights (declared now, used from Day 80), with stable ids and generation checks like the RHI's handles.
- [x] Frustum culling (sphere, then AABB) unit-tested including objects straddling planes, behind the camera and at near/far; a timing smoke test for 100k objects recorded, not asserted.
- [x] Per-frame GPU data: a `FrameData` uniform (view, projection, view-projection, inverse view-projection, camera position, viewport size, time) and one per-object storage buffer (world matrix, normal matrix) rewritten per frame; draws select their object by instance index, so no per-draw bind group is needed (the Phase 4 hotspot).
- [x] Null-device tests for upload and destruction order; a GPU test that creates, uploads and destroys 1000 meshes with zero leaks (`GetLeakCounts`).
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** Camera, mesh, scene and frame-data types are documented in headers and `docs/guide/renderer.md` gains the concepts and the scene list. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

## Result

- `engine/renderer/{camera,bounds,frustum,mesh,render_scene,frame_data}.{h,cpp}`; the camera helpers moved out of `game/common/cube_scene.*` (kept as `using` names there), and `rhi_demo`, `rhi_bench` and the RHI tests link `renderer`.
- Tests: `tests/renderer/geometry_test.cpp` (camera, bounds, frustum, procedural meshes) and `scene_test.cpp` (null-device upload/destroy order, ids and slot reuse, lights, culling, frame buffers, GPU 1000-mesh create/destroy). 43 renderer test cases pass.
- 100k-object cull: 1172 visible, about 3 ms on this machine (recorded, not asserted).
- The 1000-mesh GPU test runs with validation layers and the LeakSanitizer flavor; it does not call `GetLeakCounts`, because `tests/renderer` must not name Vulkan types (the boundary check). The null-device test asserts zero live buffers after destruction.
- Docs: lint 1979/1979 (baseline ratcheted), guide `docs/guide/renderer.md` gains "Meshes, cameras and the scene" with a compiled sample; `docs/reference/` regenerated.

## Post-implementation code review

`/code-review high`: 8 findings.

- **Fixed:** `RenderScene::GetObject` collides with the Win32 `GetObject` macro (Phase 6): renamed `FindObject`, `GetLight` to `FindLight`.
- **Fixed:** `SetLookAt` compared an unnormalized cross product to an absolute epsilon: now relative to `|up|`, and a zero `up` aborts. Regression test `LookAt does not depend on the length of up`.
- **Fixed:** `Frustum::FromViewProjection` divided by an unchecked plane length: now aborts on a zero or non-finite matrix (documented `Aborts`).
- **Fixed:** `MeshData::PositionAt` could return a misaligned `f32*`: `Set` now requires stride and position offset to be multiples of 4. Regression test `MeshData::Set rejects a position that is not 4-byte aligned`.
- **Declined:** `tests/rhi/CMakeLists.txt` derives the renderer flavor from the RHI library name. The renderer tests derive flavors the same way, and the three RHI flavors are the only callers.
- **Declined:** the `using renderer::...` shim in `cube_scene.h`. It keeps three callers unchanged; the demo helpers are reworked in the next days.
- **Declined (documented):** the frame number is a float in `FrameData::time.z`; exact to 2^24 frames. The header now says so.
- **Process finding:** STATUS.md and this file had not yet been updated: done in this commit.

**Done:** Meshes, cameras, culling and the scene list work and are tested; per-object data reaches shaders through one buffer indexed per draw.
