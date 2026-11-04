# Phase 4 / Day 78 — The G-buffer Geometry Pass

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 77.

The first real renderer pass: draw every visible object into the G-buffer with its material, depth-tested.

## Tasks

- [ ] Create the G-buffer targets as graph transients from the formats fixed in the spec, sized to the swapchain, validated with `FormatSupport` at renderer creation (a missing format is a logged failure naming the format, not a crash).
- [ ] Slang geometry shaders: a base `gbuffer.slang` (vertex: object matrix from the object buffer by instance index, world normal; fragment: sample albedo/normal map/material params and pack into the MRTs) with permutations for normal map and alpha test; octahedral normal packing in a shared Slang module with CPU reference functions and a round-trip test.
- [ ] The pass: cull the scene (Day 73), sort by the Day 74 key, bind pipeline/material groups only on change and issue instanced draws for runs of the same mesh and material; per-draw cost is a handful of RHI calls and no object creation.
- [ ] Depth keeps the Phase 3 convention (no reverse-Z): stored `DepthAttachment`, read in the lighting pass as `DepthRead`; near/far derived from the scene bounds by a camera helper.
- [ ] Offscreen GPU tests (golden + analytic): in a known scene (a plane, a sphere, a cube behind it) read back the G-buffer layers - albedo equals the material albedo, decoded normals match the geometry within tolerance, the nearer object wins in depth; a shuffled draw order gives identical results; culled objects do not appear.
- [ ] A debug view (`ShowGBuffer(layer)`) for the demo: albedo, normals, material, linear depth.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** The pass, the G-buffer layout and the geometry shader contract are documented in the guide and spec. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** A scene of many materials renders into a G-buffer whose layers are verified analytically and by golden image, with correct depth ordering independent of draw order.
