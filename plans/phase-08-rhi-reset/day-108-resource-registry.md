# Phase 8 / Day 108 — The Resource Registry; Recreatable Meshes & Textures

**Phase:** [Phase 8 — RHI Reset](../phase-08-rhi-reset.md)
**Depends on:** Day 106.

The renderer half begins: every device-dependent object records how to rebuild itself, and one registry rebuilds them in
order. This day covers the registry and the leaf owners (geometry, textures, samplers).

## Tasks

- [x] `renderer::DeviceResources` (`engine/renderer/device_resources.h`): a registry of `Recreatable` objects (`Release()`, `Recreate(Device*)`, a `tier` number, a name for logs); it is the renderer's `DeviceObserver` on the `DeviceHost`. Tiers (documented, fixed): 0 samplers and textures, 1 meshes, 2 layouts and shaders, 3 materials and pipelines, 4 passes and per-frame buffers; an owner states a tier, the registry sorts, `Release` runs high to low, `Recreate` low to high.
- [x] Registration is RAII (`ScopedRegistration`): an owner registers on creation and unregisters on destruction, so a destroyed owner never receives a callback; registering during a reset is rejected.
- [x] Failure policy: a `Recreate` that fails stops the sequence, returns the owner's name and reason, and the registry leaves everything released (a half-built set is never used); the caller rolls back (Day 110). `Recreate` is retried-safe: calling it again after a failure starts from released.
- [x] `GpuMesh` keeps what it needs: it registers itself, stores a `const MeshData*` borrowed or an owned copy (decide per the spec: an owning `MeshAsset` handle wrapping `MeshData` for procedural meshes, a loader callback for assets); `Release()` destroys the buffers, `Recreate()` re-uploads. Handles change after a recreate, so `VertexBuffer()`/`IndexBuffer()` are read each frame (no cached handle elsewhere; audited, with the render objects holding `const GpuMesh*` which stays valid).
- [x] `GpuTexture` (new, `engine/renderer/texture.h`): owns a `TextureHandle` and its source, either CPU pixels (all mips), or an asset path with a loader callback (`.rtex`), plus the `TextureDesc`; mip generation re-run if the source has fewer mips; `Recreate()` creates and uploads, waits on the upload ticket. `Sampler` owner likewise.
- [x] Replace the raw texture handles in `game/renderer_demo` (`demo_scene.cpp` textures) and `game/common/textures.cpp` with `GpuTexture`; `CreateTextureFromRtex` becomes the loader callback.
- [x] Capability changes between devices: a recreate on a device with smaller limits (`max_texture_dimension_2d`, missing format) fails with a clear reason or applies a documented fallback (downsample, other format); tested with a null-device caps override.
- [x] Tests (null backend): register/unregister; tier order of release and recreate; failure leaves all released and reports the owner; recreate twice; a mesh and a texture have equal content after a reset (read back through the null device's storage or the Vulkan readback in a GPU test); destroying an owner during no reset is fine; registering during a reset is rejected; counts at zero after destruction; caps shrink case; ASan/TSan.
- [x] **Documentation:** `///` labels; `renderer.md` "Surviving a reset" section: the owner contract (keep the source, register, never cache a handle past a frame), with a tagged sample; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected` plus asan and tsan; quote the lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`, extra scrutiny on ownership of the CPU source and destruction order; record it.
- [x] Update `STATUS.md`; commit.

**Done:** meshes, textures and samplers survive a device change with identical content, the registry orders and reports failures as specified, the demo's textures are `GpuTexture`s, and the owner contract is documented.

## Result

Added `renderer::DeviceResources` (tiered registry, a `rhi::DeviceObserver`; `Recreatable`, `ScopedRegistration`, `tiers`), `GpuMesh` as a `Recreatable` (`MakeRecreatable` with a borrowed source or a loader, `AdoptSource`, `Release`, `Recreate` that restores the ready state), `GpuTexture` (pixels or loader, GPU mip generation when the source has one level, limit and format checks) and `GpuSampler` (`engine/renderer/texture.h`), the renderer demo's textures and meshes as registered owners (`SceneOptions::resources`), guide "Surviving a reset" with a tagged sample, `tests/renderer/device_resources_test.cpp` (14 cases, one on a real device comparing texture pixels before and after a reset).
Deviation: the demo's materials still bind raw texture handles, so the demo scene does not render correctly after a reset until Day 109 makes materials bind by owner (filed in `KNOWN-ISSUES.md`); `rhi_demo`'s own cube texture was not converted (it is an RHI demo, not renderer code).
Scoped runs: plain `ran 980 of 2593 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 window test failed), asan `ran 707 of 2593 ... flavors: asan`, tsan `ran 434 of 2593 ... flavors: tsan`; docs 4/4.

## Post-implementation code review

9 findings. Fixed: dangling `debug_name` after a move (sampler and texture); a recreated mesh now goes back to its ready state; `Unregister` during a reset is deferred (entries are tombstoned and compacted); registration state comes from the owner's `Live()` (an owner registered before the registry saw a device was skipped and later hit an assert); a failed registration rolls the texture or sampler back (the mesh stays valid, documented); `rhi::MaxMipLevels` reused; sources are not kept when no registry is given; regression tests for each. Declined: the per-mesh size and the O(n) `Rebind` on move (a few hundred meshes at most; revisit if a scene holds thousands); the demo's stale material handles are Day 109's work (filed).
