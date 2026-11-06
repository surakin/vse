# Phase 7 / Day 108 — The Resource Registry; Recreatable Meshes & Textures

**Phase:** [Phase 7 — RHI Reset](../phase-07-rhi-reset.md)
**Depends on:** Day 106.

The renderer half begins: every device-dependent object records how to rebuild itself, and one registry rebuilds them in
order. This day covers the registry and the leaf owners (geometry, textures, samplers).

## Tasks

- [ ] `renderer::DeviceResources` (`engine/renderer/device_resources.h`): a registry of `Recreatable` objects (`Release()`, `Recreate(Device*)`, a `tier` number, a name for logs); it is the renderer's `DeviceObserver` on the `DeviceHost`. Tiers (documented, fixed): 0 samplers and textures, 1 meshes, 2 layouts and shaders, 3 materials and pipelines, 4 passes and per-frame buffers; an owner states a tier, the registry sorts, `Release` runs high to low, `Recreate` low to high.
- [ ] Registration is RAII (`ScopedRegistration`): an owner registers on creation and unregisters on destruction, so a destroyed owner never receives a callback; registering during a reset is rejected.
- [ ] Failure policy: a `Recreate` that fails stops the sequence, returns the owner's name and reason, and the registry leaves everything released (a half-built set is never used); the caller rolls back (Day 110). `Recreate` is retried-safe: calling it again after a failure starts from released.
- [ ] `GpuMesh` keeps what it needs: it registers itself, stores a `const MeshData*` borrowed or an owned copy (decide per the spec: an owning `MeshAsset` handle wrapping `MeshData` for procedural meshes, a loader callback for assets); `Release()` destroys the buffers, `Recreate()` re-uploads. Handles change after a recreate, so `VertexBuffer()`/`IndexBuffer()` are read each frame (no cached handle elsewhere; audited, with the render objects holding `const GpuMesh*` which stays valid).
- [ ] `GpuTexture` (new, `engine/renderer/texture.h`): owns a `TextureHandle` and its source, either CPU pixels (all mips), or an asset path with a loader callback (`.rtex`), plus the `TextureDesc`; mip generation re-run if the source has fewer mips; `Recreate()` creates and uploads, waits on the upload ticket. `Sampler` owner likewise.
- [ ] Replace the raw texture handles in `game/renderer_demo` (`demo_scene.cpp` textures) and `game/common/textures.cpp` with `GpuTexture`; `CreateTextureFromRtex` becomes the loader callback.
- [ ] Capability changes between devices: a recreate on a device with smaller limits (`max_texture_dimension_2d`, missing format) fails with a clear reason or applies a documented fallback (downsample, other format); tested with a null-device caps override.
- [ ] Tests (null backend): register/unregister; tier order of release and recreate; failure leaves all released and reports the owner; recreate twice; a mesh and a texture have equal content after a reset (read back through the null device's storage or the Vulkan readback in a GPU test); destroying an owner during no reset is fine; registering during a reset is rejected; counts at zero after destruction; caps shrink case; ASan/TSan.
- [ ] **Documentation:** `///` labels; `renderer.md` "Surviving a reset" section: the owner contract (keep the source, register, never cache a handle past a frame), with a tagged sample; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` plus asan and tsan; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`, extra scrutiny on ownership of the CPU source and destruction order; record it.
- [ ] Update `STATUS.md`; commit.

**Done:** meshes, textures and samplers survive a device change with identical content, the registry orders and reports failures as specified, the demo's textures are `GpuTexture`s, and the owner contract is documented.
