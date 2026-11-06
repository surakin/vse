# Phase 4 / Day 63 — Depth Buffer, Indexed Draws & the Spinning Cube

**Phase:** [Phase 4 — Vulkan RHI](../phase-04-vulkan-rhi.md)
**Depends on:** Days 61, 62.

3D: depth testing, index buffers and a transformation uniform, using the foundation math types.

## Tasks

- [x] Depth texture creation per `DeviceCaps::depth_format`, `ResourceState::DepthAttachment`, depth clear/load ops in `BeginRendering`, depth state in the pipeline (compare op, write enable); depth texture recreated with the swapchain on resize.
- [x] Cube geometry in the demo: 24 vertices (position, normal, uv) + 36 indices uploaded through staging; vertex layout in the pipeline.
- [x] Per-frame uniform data (model-view-projection from `Mat4`/`Transform` of Phase 0: perspective matrix with the Vulkan clip-space conventions — Y flip and 0..1 depth — implemented as a documented `Mat4::PerspectiveVulkan`-style helper in the demo/renderer-side code, **not** in the RHI) via `UniformRing` + a bind group.
- [x] Shaders `cube.vert` (MVP transform, pass normal/uv/color) and `cube.frag` (flat vertex color for now).
- [x] Demo draws the rotating cube with depth testing; resize keeps the aspect correct.
- [x] Tests `[gpu]` offscreen: render the cube from a fixed angle at 128×128 and compare with a golden; analytic assertions — the near face covers the center pixel with its color, a second cube drawn behind is occluded (depth test) and appears when depth test is disabled; indexed vs non-indexed draws of the same geometry produce identical images; winding/cull: back faces culled.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** depth/indexed APIs documented; the conventions (clip space, handedness, winding) are written down in `docs/guide/rhi.md` with a pointer from `docs/guide/foundation-math.md`.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** a depth-tested, indexed, transformed cube renders correctly in the window and offscreen with golden and analytic checks.

## Result

Depth attachments, depth state in pipelines and indexed draws were already in the RHI (Days 55, 57 and 59); this day added the renderer-side 3D code and verified the whole path. `game/common/cube_scene.{h,cpp}` (shared by `rhi_demo` and the tests, not part of the RHI) has `PerspectiveVulkan` (right-handed view space, Y flipped, depth 0..1), `LookAt`, `ModelMatrix`, the 24-vertex / 36-index cube and `CubeRenderer` (shaders `cube.vert`/`cube.frag`, bind group layout, pipeline, vertex and index buffers uploaded through staging, per-draw `UniformRing` slice + transient bind group). `rhi_demo` draws the spinning cube with a depth buffer that follows the swapchain size (checked on Wayland and X11 with validation, clean) and `--screenshot` renders it at a fixed angle. Tests (`tests/rhi/vulkan_cube_test.cpp`): the projection, view and mesh winding without a GPU; the cube at 128x128 from a fixed angle against `tests/rhi/golden/cube.ppm` (inspected by eye) plus three face-center probes projected on the CPU; a larger cube behind the first is occluded with the depth test and overdraws without it; indexed and non-indexed draws give byte-identical images; cull back / front / none give the expected near or inside-far face. Guide: "Depth, indexed draws and the 3D conventions" in `rhi.md`, with a pointer from `foundation-math.md`. Docs 1683/1683.

One finding about conventions, recorded because the plan assumed otherwise: with the Y-flipping projection, front faces for meshes authored counter-clockwise are `FrontFace::CounterClockwise` (the default). The first attempt used `Clockwise` and drew the inside of the cube; the golden test now fixes the convention.

## Post-implementation code review

`/code-review high` returned 8 findings.

Fixed: (1) `Destroy` resets `geometry_ready_`, so a re-created renderer records its upload barriers again; (2) `Prepare` is documented as once per list and the test no longer calls it twice for one renderer; (3) `CubeRenderer` has a destructor calling the idempotent `Destroy`, so a failed REQUIRE does not leak into the device teardown; (4) `RecordFrame` returns whether the cube was drawn and the demo reports a failure (exit code 1 in screenshot mode), and its unused parameter is gone; (6) the depth store op is `DontCare` in the demo and tests; (7) `Project` requires an on-screen point in front of the camera before converting; (8) stray option assignments removed; (9) day file and `STATUS.md`.

Declined: (5) building `cube_scene.cpp` as a library: it is two source files used by the demo and by each sanitizer flavor of the RHI tests, which must link their own flavor of the RHI library, so one prebuilt library would mix flavors; compiling it into each target keeps the flavors consistent.
