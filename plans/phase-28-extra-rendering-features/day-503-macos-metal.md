# Phase 28 / Day 503 — macOS: Metal for the Phase's Features (the user's day)

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 479-502.

Build and run everything on the Mac; the code is written there.

## Tasks

- [x] Metal cube textures (`MTLTextureTypeCube`, `replaceRegion` per face and slice, `Caps::cube_textures = true`, the `Unsupported` return removed); the RHI cube test passes with the Metal validation layer on (`MTL_DEBUG_LAYER=1 ...` as in CLAUDE.md).
- [x] Re-mint the Metal libraries (`tools/metal_mint`) for the sky, lighting, terrain and instanced shaders (the `main` rename and the clip-space Y flip per `shaders/README.md`); `shaders_check` passes; `KNOWN-ISSUES.md` rows removed.
- [x] Vertex-stage texture reads of the height map and the instance buffer on Metal (argument limits, `memoryless` not applicable); the terrain and instancing render tests pass validated.
- [x] Physics determinism: the terrain scene's hash equals Linux's and the PC's on x86-64; record it.
- [x] Build clean with Apple clang, unity on; plain tests through `scripts/test-phase.sh --affected` (no sanitizer flavors on the Mac for now, no unity-off or arm64 builds, no script demo, per the standing notes); record the `ran X of Y` line.
- [x] Day review on the Mac-side diff; `STATUS.md` and `KNOWN-ISSUES.md` current.

**Done:** The phase's features build, run and pass on macOS with Metal validation; the physics hash matches Linux and Windows.

## Notes

- **Done 2026-11-23 (the Mac, `build-macos`, unity, plain).** Metal cube textures: `MTLTextureTypeCube` in `MetalDevice::CreateTexture` (`arrayLength` 1; the six faces are the slices), `caps_.cube_textures = true`; the RHI cube test passes validated. Re-minted with `cmake --build build-macos --target shaders`: `lighting.frag`, `sky.frag`, `sky_cube.frag`, `cube_sample.comp`, `terrain.vert`, `terrain.frag` (no instanced shaders exist yet). The terrain and sky render tests, the far-terrain test, the scene tests and `engine_c_terrain` pass validated, so the vertex-stage height reads work without argument-limit changes. The terrain scene's physics hash is the golden table's constant, which passes on the Mac, so it equals Linux's.
- **Run:** `scripts/test-phase.sh --affected --since 89cdb466f6ac --display` (everything, the foundation math headers changed): `ran 4340 of 11692 tests (phases: all; flavors: plain; display: included; crash tests: excluded; soak: excluded) in 749 s`. Two failures: the voice-cost snapshot (known) and the DXIL staleness check (the PC re-mints `lighting.frag`'s DXIL with Day 502).
- **Found and fixed:** `tests/script/entity_bindings_test.cpp` set a position past the 1e12 limit (now 1e11).
- Not built: the arm64 and unity-off trees (the user's rule).

- **Second pass, 2026-11-24 (after Days 495-501, the instancing):** `gbuffer.vert`'s Metal library re-minted (the only stale one). **Found:** the G-buffer pass refused every instanced object where the object index comes from the index stream (Direct3D 12 and Metal): "instanced meshes are not drawn on this backend yet" (`engine_c_instances` and four instancing render tests failed on Metal; the same refusal waits for the PC's Day 502). **The PC fixed it the same way in parallel (Day 502, `8d37494f`); at the merge the Mac took the PC's code and kept only the Metal library and the records. The Mac's own fix was:** an instanced draw binds the stream too (an instanced pipeline already declares the attribute; the draw reads `first_instance` to `first_instance + n - 1`, which the identity stream returns), the stream holds 262,144 indices (it held 65,536, less than a frame's default 262,144 instances; a `static_assert` ties the two), and a frame whose instance budget is larger than the stream is refused with a message. All 65 instancing tests pass on Metal, validated. **Run:** `scripts/test-phase.sh --affected --since b505be32^ --display`: `ran 4402 of 11760 tests (phases: all; flavors: plain; display: included; crash tests: excluded; soak: excluded) in 733 s`; failures: the voice-cost snapshot (known), the DXIL staleness check (the PC), and `docs_check` (the reference was out of date after the header comment changed; regenerated, passes).

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

`/code-review high` on the Mac-side diff: 5 findings, all verified.

- **Fixed (real bug):** `CopyBufferToTexture` and `CopyTextureToBuffer` on Metal asserted `array_layer < t.arrayLength`, and a cube's `arrayLength` is 1, so copying faces 1 to 5 aborted. A `SliceCount` helper (6 for a cube) bounds the layer now. New test `Every face of a cube texture is copied from a buffer and back` (`tests/rhi/cube_texture_test.cpp`), which fails without the fix (checked) and passes with it.
- **Fixed:** the Metal cube path was tested through `UploadTexture` and sampling only (the same finding; the new test covers the copies and the readback of every face).
- **Fixed:** nothing tested the 1e12 limit of script positions after the far-position test moved to 1e11; the new test `Entity bindings: a position past the world limit is refused and the entity stays` refuses 1e12 + 1 and accepts 1e12.
- **Fixed:** `DeviceCaps::cube_textures`'s comment and `docs/guide/rhi.md` said Vulkan and the null device only; both name Metal, and `docs/reference` is regenerated (`docgen` 14191 of 14191).
- **Declined, no change:** the minted libraries' reproducibility. `shaders_check` mints every library twice and compares, and passes (it compares the committed libraries only on a Mac of the same macOS build, which is this one).

Runs after the fixes: `ctest -L "docs|rhi"` (plain) 267 of 267 pass.

**Second review (instancing pass), 6 findings:**

- **Fixed:** refusing the whole G-buffer pass for a frame whose instance budget exceeds the stream, even with no instanced objects. Now `GBufferPass::InstanceRoom()` caps the budget at the stream's size on devices that use it, so such a frame draws, with the rest counted as dropped.
- **Fixed:** the plan note claimed a `static_assert` tying `kMaxObjectIndices` to `kDefaultFrameInstances`; it was missing. It is in `gbuffer.cpp` now.
- **Fixed:** the records (day file, `KNOWN-ISSUES.md`, `STATUS.md`) were not in the diff yet; they are now.
- **Declined:** no null-device test of an instanced draw or of a budget above the stream: `renderer_demo::FrameSetup` has no instance budget and the instanced scenes need real draws; the 65 instancing tests, `engine_c_instances` and the render tests cover the path on Metal with validation, and the PC's Day 502 will on Direct3D 12.
- **Declined:** the stream is 1 MiB instead of 256 KiB on Direct3D 12 and Metal (one buffer, built at creation and on a reset): a small cost against a stream that holds a frame's default instances.
- **Declined:** the lighting pass's own stream use (`point_draws_`, one draw per light): unchanged by this work, and a larger stream only widens what it may read.

Runs after the fixes: `ctest -L "renderer|rhi|scene|engine_library|docs"` (plain) 775 tests, 774 pass; the failure is the DXIL staleness check (the PC).
