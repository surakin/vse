# Phase 4 / Day 81 — Point Lights & Light Volumes

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 80.

Many local lights without a fullscreen pass per light.

## Tasks

- [x] `PointLight` (position, radius, color, intensity) with a documented attenuation function that reaches exactly zero at the radius (so the volume is a correct bound); `LightData` holds up to a documented maximum and overflow is an error, not silent truncation.
- [x] Light volume pass: draw a sphere mesh per visible light, additive blend into the accumulation target, front faces culled so a camera inside a volume still lights, depth test off, scissor to the light's screen bounds; the shader reads the G-buffer as for directional light; lights are frustum-culled with the Day 73 tests and sorted deterministically.
- [x] Light data path: one storage buffer of lights indexed by instance id; no per-light bind group or pipeline change.
- [x] Tests: a single point light on a plane matches the analytic falloff at probe pixels; a camera inside a light volume is still lit; zero contribution outside the radius; N overlapping lights sum linearly (tolerance); a culled light issues no draw (counter).
- [x] Scale measurement (recorded, not asserted): 1, 100 and 1000 lights at 1080p offscreen, frame time and draw counts noted in the day file.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** Point lights, the attenuation formula, limits and the light volume pass are documented in the guide. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

## Result

- Attenuation: `PointLightAttenuation(d, R) = saturate(1 - (d/R)^4)^2 / (d^2 + 1)` (`lighting_common.slang` `PointAttenuation`, CPU twin in `lighting.cpp`): 1 at the light, exactly 0 at and beyond the radius, zero slope there. Limits: `kMaxLights` = 16384 for `RenderSceneDesc::max_lights` and `FrameResources` (abort above it); lights that do not fit are counted in `LightSummary::dropped` and an error is logged once (directional first, then the nearest point lights). `AddLight`/`SetLight` also abort on non-finite color, position or direction.
- Data path: `WriteLights(scene, frame_data)` writes directional lights, then the visible point lights (frustum-culled with `Frustum::TestSphere`, `LightSummary::point_culled`) sorted by squared distance from the camera, ties by slot; one CPU-side copy (`WrittenLights()`) because the mapped buffer is write-only memory. One storage buffer, indexed by the instance index; no per-light bind group or pipeline change.
- Volume pass: `LightingPass::Prepare(scene, view)` plans one draw per visible light with its `LightScreenRect` scissor (whole target if the sphere's bounding box reaches the camera plane; empty rectangle: no draw); `Record` draws the fullscreen directional/ambient triangle, then binds the `point_light` pipeline once (additive blend `One/One`, `CullMode::Front`, no depth attachment, no vertex buffer) and issues one `Draw(768, 1, 0, light_index)` per light. Counters `PointDrawsPlanned()`/`PointDrawsRecorded()`. New shaders `point_light.vert` (the UV sphere is generated from `SV_VulkanVertexID`, scaled by `LightVolumeScale()` so its faces stay outside the light's sphere; clip z clamped into 0..w so near/far planes never clip it) and `point_light.frag` (reads the G-buffer like the directional shader, discards where the attenuation is 0).
- Deviation from the task text: the sphere "mesh" is generated in the vertex shader (CPU twin `LightVolumeVertex`, 16 slices x 8 rings, same vertex order and winding as `MakeSphereMesh`) instead of a `GpuMesh`: no vertex buffer, no upload barriers to carry through the pass, and the CPU twin lets a test prove containment of the sphere and the winding.
- Tests: `tests/renderer/lighting_test.cpp` (attenuation values/monotonic/zero beyond the radius, volume containment and CCW winding for all 768 vertices, screen rectangle, culling and nearest-first order, deterministic ties, overflow counting, limit) and `tests/rhi/vulkan_lighting_test.cpp` (validation and sync validation silent): a point light on a plane matches the analytic model at 8 probes and is exactly 0 outside the radius; a camera inside a light volume lights every pixel; 12 overlapping lights sum linearly and 3 culled lights issue no draw (draws planned/recorded = 12); an all-culled scene records no draw; a light larger than the far plane still lights its interior.
- Scale measurement (recorded, not asserted; wall time submit to idle, mean of 10 frames after one warm-up, release build, NVIDIA GPU, 1920x1080 G-buffer + lighting + tonemap, lights of radius 2.5 over a 24x24 plane): 1 light 0.59 ms, 100 lights 1.1 ms, 1000 lights 5-6 ms; draw counts 1, 100, 1000 (none culled). GPU-only time was not measured (no timestamp queries in the RHI yet), see `KNOWN-ISSUES.md`.
- Mutations checked: with the winding reversed the camera-inside test and the CPU winding test fail (the outside-camera test cannot see it); with the clip-z clamp removed the large-light test fails.

## Post-implementation code review

`/code-review high`: 8 findings.

- **Fixed:** a camera inside a large light lost the far side of the volume to far-plane clipping (the lit region under it stayed dark). The vertex shader clamps clip z into 0..w. Regression test (a radius-50 light with a far plane at 30) fails without the clamp.
- **Fixed:** a NaN position or color got through culling, the sort comparator (undefined behaviour in `qsort`) and the scissor. `AddLight`/`SetLight` now abort on a non-finite color, position or direction. No test: the check is an `ENGINE_ASSERT` abort and the test suite has no death-test facility.
- **Fixed:** `Prepare` took the target size from the frame block with nothing tying it to `AddToGraph`'s size; `AddToGraph` now asserts they match and `Prepare` asserts a viewport of at least 1 pixel (documented under Aborts).
- **Fixed:** `PointDrawsRecorded` was written by the recording callback (possibly on a worker) and read on the frame thread; it is now a relaxed atomic.
- **Fixed:** the visible-light scratch list is reserved once in the constructor (sized to `max_lights`) so the per-frame path does not allocate in the normal case; construction fails (not valid) if it cannot.
- **Declined:** sorting every frame. Nearest-first order is the documented contract (deterministic output, and which lights survive an overflow); the cost at 1000 lights is included in the measured frame time and is a small part of it.
- **Declined:** "no test that every face is outside the radius". There is one (`min_face_distance >= 1` over all 768 vertices); the finding was wrong. The repeated `cos` in the CPU twin is test-only code.
- **Process:** this file, `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit.

**Done:** Hundreds of point lights light a scene correctly in additive volumes, verified against the analytic model, with limits enforced.
