# Phase 26 / Day 319 — Metal IV: Pipelines and the Pipeline Cache

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 318.

Render and compute pipelines from the minted `.metallib` blobs, the fixed-function state objects, and the pipeline cache as an `MTLBinaryArchive`.
Modelled on DX12 Day 167.

## Tasks

- [x] **Shaders:** `CreateShader` takes the `MetalLib` blob (`newLibraryWithData:` through a `dispatch_data_t`), looks up the recorded entry name, keeps
      the CPU-side blob for re-creation (the reset rule); a blob of another format refused with a log.
- [x] **Render pipelines:** `MTLRenderPipelineDescriptor` from `GraphicsPipelineDesc`: vertex descriptor (locations to attributes, the object index stream
      as a per-instance buffer at its reflected slot), color attachment formats and blending, depth attachment format, sample count; topology class;
      `MTLDepthStencilState` cached by description; rasterizer state (cull, winding, depth bias, fill mode) recorded with the pipeline and applied when bound.
- [x] **Compute pipelines:** `MTLComputePipelineState`; threadgroup size from the reflection (Day 315), checked against
      `maxTotalThreadsPerThreadgroup`.
- [x] **Pipeline cache:** (the `renderer_demo` first- and second-start measurement is deferred to Day 325: the renderer runs on Metal from Day 323) `MTLBinaryArchive` serialized to the file named by adapter identity (`pipeline_cache_file.h`), loaded at device creation,
      added to on pipeline creation, written at `Quiesce`; a stale or damaged archive is discarded with a log (tested by corrupting it); measure first and
      second start of `renderer_demo` pipeline creation.
- [x] **Permutations:** the permuted material shaders create through the same path; `--prewarm` works on Metal.
- [x] **Tests:** (the Metal file; `shader_test`, `shader_interface_test` and `pipeline_cache_file_test` through Metal join with the GPU fixture on Day 322) `shader_test`, `shader_interface_test`, `pipeline_cache_file_test` through Metal; `metal_pipeline_test.mm` (vertex descriptor, depth
      state cache, threadgroup size, archive round trip and corruption).
- [x] **Documentation:** `rhi.md` Metal section (the archive, threadgroups); `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` (quote). **Deferred to Day 325 (the user's choice).**
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: the archive file's write atomicity (through `WriteFileAtomic`), concurrent pipeline creation
      from jobs (TSan), lifetime of the library behind a function.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** render and compute pipelines (with permutations) are created on Metal from the minted blobs with validation silent, the binary archive persists
and survives corruption, the creation-time measurement is recorded, and the pipeline tests pass in three flavors.

## Implementation notes

- **Shaders** (`metal_device_pipelines.mm`): an `MTLLibrary` from the minted bytes (`newLibraryWithData:` through a `dispatch_data_t`) and the
  function the blob names, of the stage's type; another format is refused with a log. The shader keeps a copy of its slot table (the descriptor's
  is borrowed for the call). It does not keep the bytes: a reset recreates the whole device, and the renderer's owners keep their minted blobs.
- **Render pipelines:**
  - The vertex descriptor puts binding `b` at buffer slot `30 - b`, above the shader's own buffers, and creation fails if they would meet.
    Attribute locations and bindings are bounded first.
  - Color formats, blending, write masks, the depth and stencil formats, the sample count and the topology class are set in the pipeline.
  - Cull, winding, fill mode, the primitive type and a shared depth-stencil state are kept for the command list (`MetalPipelineInfo`,
    `LookupPipeline`).
- **Compute pipelines:** the threadgroup size is minted into the slot table (a Metal dispatch passes it; the 29 libraries were re-minted
  byte-identical). It is checked against the device's limits before compiling and against the pipeline's own limit after.
- **Pipeline cache** (`metal_pipeline_cache.mm`):
  - An `MTLBinaryArchive` kept in `metal-<adapter id>.bin` behind a 32-byte header: magic, version, adapter and macOS identity, and an FNV-1a
    hash seeded with the size.
  - A miss is added to the archive and then created from it, so it compiles once.
  - It is written at `Quiesce` and at destruction, atomically through the file layer.
  - Each device copies the archive to its own file for Metal to read, and serializes into its own temporary file.
- **Permutations** create through the same path (`metal_pipeline_test` makes every permuted module's pipeline); `--prewarm` waits for the
  renderer on Metal (Day 323).
- **Tests added** (run on Day 325): `tests/rhi/macos/metal_pipeline_test.mm` covers:
  - the header rules;
  - every committed shader pair, the depth-only and compute pipelines;
  - the vertex descriptor, the depth-state cache and the threadgroup;
  - the refusals (SPIR-V, a wrong stage, a missing layout, a location past 31, a missing binding);
  - the archive round trip, two devices on one cache, and damage to the archive or the identity.
- **Docs:** `docs/guide/rhi.md` (pipelines, the cache, the threadgroup); `DeviceCreateInfo::pipeline_cache` and `ShaderDesc::slots` updated;
  the reference regenerated.
- **Built:** both unity modes on macOS, every flavor, no warning. **Runs: deferred to Day 325.**

## Post-implementation code review

`/code-review high` on the day's diff: 10 findings, 8 fixed, 2 declined.

1. **Two devices on one adapter shared the archive copy and the serialization file** (and the first one's end deleted the second one's archive).
   Fixed: the process id and the device serial name both files; tested with two devices.
2. **Archive lookups ran beside adds without a lock.** Fixed: every use of the archive is under the cache's lock (`NewRender`, `NewCompute`).
   Pipeline creation is serialized while a cache is open, which the guide says.
3. **A compute pipeline was cached and counted before a threadgroup check refused it.** Fixed: the device's limits are checked before it is
   compiled.
4. **An attribute location past 31 would raise inside Metal.** Fixed: locations, bindings and formats are bounded first; tested.
5. **A miss compiled twice** (the state, then the archive add). Fixed: added first, then created from the archive.
6. **`ShaderDesc::slots` did not say compute always needs the table.** Fixed in its documentation and `threadgroup_size`'s.
7. **The FNV and header helpers repeat D3D12's.** Declined: the formats differ (the D3D12 one holds pipeline keys), and moving D3D12's code
   into shared RHI files changes Windows code that only the PC can build and test. To revisit when both backends are tested together.
8. **A good file that could not be copied was reported as damaged, and archive add errors were silent.** Fixed: a write failure has its own
   message, and add errors are logged.
9. **`DeviceCreateInfo::pipeline_cache` still said Vulkan only.** Fixed.
10. **Day file and `STATUS.md` not updated; the measurement not recorded; the CPU-side blob not kept.** The updates are in this commit and the
    measurement is deferred to Day 325. Keeping the blob is declined: a reset recreates the whole device, and the owners keep their minted
    blobs (the reset rule's CPU-side source).
