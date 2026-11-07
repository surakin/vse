# Phase 12 / Day 160 — DX12 IV: Pipelines and the Pipeline Cache

**Phase:** [Phase 12 — Windows Port + DX12 Backend](../phase-12-windows-dx12.md)
**Depends on:** Day 159.

Graphics and compute pipelines on D3D12 and a persistent cache named by adapter identity.

## Tasks

- [ ] **Shaders:** `CreateShader` accepts `ShaderFormat::Dxil` blobs (and refuses SPIR-V with the shader's name); the blob is kept for pipeline creation and for
      rebuilds after a reset (CPU-side source rule).
- [ ] **Graphics pipelines:** input layout from the RHI vertex layout (semantic names per the spec's convention matching what `slangc` emits), primitive topology
      type and the per-draw topology, rasterizer (cull, front face — check the winding convention against the Y-flip decision of Day 156), depth/stencil, blend
      per attachment, render-target and depth formats, sample count; `CreateGraphicsPipelineState`; failures logged with the pipeline's debug name and the debug
      layer's message.
- [ ] **Compute pipelines:** `CreateComputePipelineState`; thread-group size comes from the shader (no RHI change needed).
- [ ] **Pipeline cache:** `ID3D12PipelineLibrary` (or cached PSO blobs, decided here by measurement) stored under `UserCacheDirectory()` with a file name carrying
      vendor, device and driver identity (`BACKEND-LIFECYCLE.md` rule); a driver change or a different adapter writes its own file; a corrupt or mismatching file
      is discarded with a log line, never a crash. Measure cold versus warm creation of the renderer's pipelines.
- [ ] **Tests** (`tests/rhi/dx12_pipeline_test.cpp`, phase 12, and shared pipeline tests by backend): every committed shader builds a pipeline with its layout,
      invalid combinations fail cleanly, the cache file round-trips, a truncated cache file is discarded, debug layer silent.
- [ ] **Runs:** Windows `--affected` plain and asan.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: cache-file handling of untrusted contents, winding/cull conventions, input-layout semantic names.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every shader the engine ships forms graphics or compute pipelines on D3D12, and the pipeline cache persists per adapter and survives corrupt files.
