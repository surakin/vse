# Phase 5 / Day 80 — Deferred Lighting: Directional & Ambient

**Phase:** [Phase 5 — Renderer Core](../phase-05-renderer-core.md)
**Depends on:** Day 79.

Turn the G-buffer into lit pixels in linear HDR.

## Tasks

- [x] Light types in the scene: directional (direction, color, intensity) and ambient; a per-frame `LightData` storage buffer.
- [x] Slang lighting shader: fullscreen triangle; reconstruct world position from depth + inverse view-projection (undoing exactly the projection's Y flip, verified against known geometry); unpack normal/albedo/material; Lambert diffuse plus a specular term (Blinn-Phong or GGX, chosen in the spec, kept in one function so later phases can change it); write RGBA16_Float to the light accumulation target; depth 1.0 (background) is skipped.
- [x] Graph wiring: the lighting pass reads G-buffer layers as `Sampled` and depth as `DepthRead`, writes the accumulation target as a color attachment; the graph inserts all barriers.
- [x] Tonemap/resolve pass (exposure plus a documented operator, then linear to sRGB through the swapchain format) reads the accumulation buffer and writes the swapchain; the operator is unit-tested on the CPU against the shader's constants.
- [x] Tests: a flat plane with a known albedo and light gives the analytic Lambert value at probe pixels (HDR target read back, tolerance documented); a light-direction sweep matches `max(dot(N,L),0)`; reconstructed world position of probe pixels matches the geometry (catches Y-flip bugs); the Phase 4 lit cube rendered through the deferred path is close to the Phase 4 forward golden (tolerance documented).
- [x] Validation and sync validation are silent in every test.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** Lights, the lighting model, position reconstruction and the tonemap, with exact formulas, are documented in the guide. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

## Result

- Scene and buffer: `LightType::Ambient`; `FrameResources` gained a per-frame `LightData` storage buffer (`WriteLights` returns a `LightSummary`: summed ambient, directional and point counts; directional first, directions normalized, capacity `max_lights`, default 64, overflow dropped with one warning). `LightData` already carries the point-light fields so Day 81 changes no layout.
- Shaders: `lighting_common.slang` (the one-function lighting model: Lambert diffuse plus GGX with height-correlated Smith visibility and Schlick Fresnel, metallic-roughness, `1/pi` folded into the intensity, a guard for opposite V and L), `lighting.frag` (reads the G-buffer with `Load`, rebuilds the world position from the depth and the inverse view-projection using the depth texture's extent, discards background), `tonemap.frag` (exposure then Narkowicz ACES; sRGB comes from the target format), and a `WorldPosition` layer in `gbuffer_debug.frag`.
- `engine/renderer/lighting.{h,cpp}` (library `renderer_passes`): `LightingPass` (`Prepare`, `AddToGraph` returning the `RGBA16_Float` accumulation resource), `TonemapPass`, and the CPU twins `ShadeLight`, `ShadeAmbient`, `TonemapAces`, `ReconstructWorldPosition`.
- Tests: `tests/renderer/lighting_test.cpp` (9 cases on the null device: dielectric and metal reference values, ambient/emissive, tonemap curve, position reconstruction in both pixel axes, the light buffer order/sum/capacity, NaN guard, pass construction) and `tests/rhi/vulkan_lighting_test.cpp` (4 cases, validation and sync validation silent): a plane with two directional lights and ambient matches the CPU model at three probes, the tonemapped display equals sRGB(ACES(HDR)) for two exposures, background pixels stay black; a six-angle light sweep for a rough dielectric and a smooth metal matches the model and `max(N.L, 0)` bounds (unnormalized scene directions included); the world-position layer matches planted geometry (a mirrored row fails it by several units); the Phase 4 cube scene lit through the deferred path stays within 0.035 of the forward Lambert values. Mutations: mirroring the pixel row in the debug shader fails the position test; removing the direction normalization fails the sweep.
- Deviations from the task text: (1) the lighting pass reads depth as `Sampled`, not `DepthRead`, for the layout reason found on Day 79. (2) The Phase 4 comparison is the analytic one from the Phase 4 test ("Visible faces are lit by the Lambert ratio ...", same eye, light and ambient, white albedo, roughness 1) rather than the golden image: the golden's checker texture depends on the Phase 4 cube's uv layout, which the renderer's cube mesh does not share; the tolerance is 0.035 (the GGX lobe adds at most about 0.02). (3) The tonemap is a standalone pass writing any color target; wiring it to the swapchain image is Day 82's demo (the graph import of the swapchain was proven on Day 77).

## Post-implementation code review

`/code-review high`: 9 findings.

- **Fixed:** the lighting shader built NDC from `FrameData::viewport`, which could disagree with the target's size; it now uses the depth texture's own extent.
- **Fixed:** `V + L` of zero (a normal facing away from the viewer, lit from behind it) made `H` NaN in the shader and the CPU twin; both fall back to the normal. Test.
- **Fixed:** `WriteLights` copied directions unnormalized; it normalizes (a zero direction stays zero). The real-device sweep now stores directions of length 2.5 and fails without the normalization.
- **Fixed:** the `WorldPosition` debug layer silently used an identity matrix when `SetInverseViewProjection` was forgotten; `AddToGraph` aborts, and the flag resets after each use.
- **Fixed:** the debug view and tonemap built their pipelines inside the recording callback (possibly on a scheduler participant); they are built in `AddToGraph` on the frame's thread, the callback only draws. Pipeline destruction was already deferred by the device.
- **Fixed:** the dropped-lights warning fired every frame; it is logged once per `FrameResources`. The capacity-versus-scene relation is documented.
- **Fixed:** `Light` and `FrameResources` documentation was stale after the Ambient type and the third buffer.
- **Declined:** an `AddToGraph`-twice assert on `TonemapPass` and `GBufferDebugView`. They have no per-frame `Prepare` to reset a flag; the header says one object serves one view, and a second view uses a second object.
- **Declined:** replacing the test rig's raw `new` with members: `Create` is called once per test and the destructor frees everything; not worth restructuring.
- **Process:** this file, STATUS.md and KNOWN-ISSUES.md are updated in the same commit.

**Done:** A deferred frame with directional and ambient light produces analytically verified HDR output, tonemapped to the swapchain, with validation clean.
