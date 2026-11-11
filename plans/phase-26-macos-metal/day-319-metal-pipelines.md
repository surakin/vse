# Phase 26 / Day 319 — Metal IV: Pipelines and the Pipeline Cache

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 318.

Render and compute pipelines from the minted `.metallib` blobs, the fixed-function state objects, and the pipeline cache as an `MTLBinaryArchive`.
Modelled on DX12 Day 167.

## Tasks

- [ ] **Shaders:** `CreateShader` takes the `MetalLib` blob (`newLibraryWithData:` through a `dispatch_data_t`), looks up the recorded entry name, keeps
      the CPU-side blob for re-creation (the reset rule); a blob of another format refused with a log.
- [ ] **Render pipelines:** `MTLRenderPipelineDescriptor` from `GraphicsPipelineDesc`: vertex descriptor (locations to attributes, the object index stream
      as a per-instance buffer at its reflected slot), color attachment formats and blending, depth attachment format, sample count; topology class;
      `MTLDepthStencilState` cached by description; rasterizer state (cull, winding, depth bias, fill mode) recorded with the pipeline and applied when bound.
- [ ] **Compute pipelines:** `MTLComputePipelineState`; threadgroup size from the reflection (Day 315), checked against
      `maxTotalThreadsPerThreadgroup`.
- [ ] **Pipeline cache:** `MTLBinaryArchive` serialized to the file named by adapter identity (`pipeline_cache_file.h`), loaded at device creation,
      added to on pipeline creation, written at `Quiesce`; a stale or damaged archive is discarded with a log (tested by corrupting it); measure first and
      second start of `renderer_demo` pipeline creation.
- [ ] **Permutations:** the permuted material shaders create through the same path; `--prewarm` works on Metal.
- [ ] **Tests:** `shader_test`, `shader_interface_test`, `pipeline_cache_file_test` through Metal; `metal_pipeline_test.mm` (vertex descriptor, depth
      state cache, threadgroup size, archive round trip and corruption).
- [ ] **Documentation:** `rhi.md` Metal section (the archive, threadgroups); `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` (quote).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: the archive file's write atomicity (through `WriteFileAtomic`), concurrent pipeline creation
      from jobs (TSan), lifetime of the library behind a function.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** render and compute pipelines (with permutations) are created on Metal from the minted blobs with validation silent, the binary archive persists
and survives corruption, the creation-time measurement is recorded, and the pipeline tests pass in three flavors.
