# Phase 13 / Day 167 — DX12 IV: Pipelines and the Pipeline Cache

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 166.

Graphics and compute pipelines on D3D12 and a persistent cache named by adapter identity.

## Tasks

- [x] **Shaders:** `CreateShader` accepts `ShaderFormat::Dxil` blobs (and refuses SPIR-V with the shader's name); the blob is kept for pipeline creation and for
      rebuilds after a reset (CPU-side source rule).
- [x] **Graphics pipelines:** input layout from the RHI vertex layout (semantic names per the spec's convention matching what `slangc` emits), primitive topology
      type and the per-draw topology, rasterizer (cull, front face — check the winding convention against the Y-flip decision of Day 163), depth/stencil, blend
      per attachment, render-target and depth formats, sample count; `CreateGraphicsPipelineState`; failures logged with the pipeline's debug name and the debug
      layer's message.
- [x] **Compute pipelines:** `CreateComputePipelineState`; thread-group size comes from the shader (no RHI change needed).
- [x] **Pipeline cache:** `ID3D12PipelineLibrary` (or cached PSO blobs, decided here by measurement) stored under `UserCacheDirectory()` with a file name carrying
      vendor, device and driver identity (`BACKEND-LIFECYCLE.md` rule); a driver change or a different adapter writes its own file; a corrupt or mismatching file
      is discarded with a log line, never a crash. Measure cold versus warm creation of the renderer's pipelines.
- [x] **Tests** (`tests/rhi/dx12_pipeline_test.cpp`, phase 13, and shared pipeline tests by backend): every committed shader builds a pipeline with its layout,
      invalid combinations fail cleanly, the cache file round-trips, a truncated cache file is discarded, debug layer silent. Moved here from Day 166: each
      committed shader's layouts are built from its reflection and its root signature comes from `Dx12Device::RootSignatureFor`, so a register or space
      that disagrees with the bindings fails the pipeline.
- [x] **Runs:** Windows `--affected` plain and asan.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: cache-file handling of untrusted contents, winding/cull conventions, input-layout semantic names.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every shader the engine ships forms graphics or compute pipelines on D3D12, and the pipeline cache persists per adapter and survives corrupt files.

## Notes

- **The cache is an `ID3D12PipelineLibrary`** (decided by the measurement below): one file per adapter and driver,
  `<cache dir>/pipelines_<adapter id>_<driver>.bin`, in the directory every backend uses (the rule and the file reading and writing moved
  out of the Vulkan backend into `rhi/pipeline_cache_file.h`; the `ENGINE_RHI_CACHE_DIR` allow-list entry moved with it). A pipeline is
  stored under a name hashed from its shaders' bytes, its root signature's serialized bytes and its whole fixed state. The file has a
  header (magic, version, the adapter and driver identity, size, FNV-1a of the rest) and the list of stored names before the library, so
  nothing is handed to D3D12 unchecked, and the device asks the library only for names it holds (asking for another makes the debug layer
  warn). Written atomically at destruction or by `SavePipelineCache`, only when it gained pipelines.
- **Measured** (Radeon 860M, debug layer on): the 33 pipelines of the committed shaders take 12.2 ms with an empty library and 10.7 ms
  from it (33 hits); one G-buffer pipeline takes 0.10 ms plain and 0.06 ms from its own cached blob (`GetCachedBlob`, 954 bytes). The
  driver keeps its own shader cache, which is why "cold" is cheap here; a first-ever compile with that cache empty was not measured
  (filed). The library was kept over per-state blobs: one file and one lookup per pipeline, against a blob store of our own per state, for
  a difference this machine cannot show.
- **Conventions checked:** vertex inputs are `TEXCOORD<location>` (the object index stream's location 15 included); `front_face` maps
  through unchanged, since the Y flip of the D3D12 build keeps the vertices where Vulkan puts them on the render target (Day 170's golden
  images confirm it); color factors in an alpha blend become their alpha counterparts (D3D12 refuses them); a strip with primitive restart
  gets a second state cutting at `0xFFFF` for 16-bit indices. Without a depth test D3D12 writes no depth, as Vulkan.
- **Shaders** keep a shared copy of their DXIL, so pipelines compile outside the device's resource lock and a shader may be destroyed
  while its pipelines live (tested).
- **Spec amended:** the root-parameter table of the Windows port spec (section 5) now describes what Day 166 built: each group's view
  table then its sampler table, an empty one left out; the indices come from `Dx12Device::RootLayout`.
- The unity build regrouped `rhi_tests` when a file was added, and `dx12_resource_test.cpp`'s `MakeDevice` met `gbuffer_rig::MakeDevice`;
  renamed to `MakeResourceDevice` (CLAUDE.md's unity rule, step 2).

## For the Linux run

Shared: `engine/rhi/pipeline_cache_file.h/.cpp` (new: the directory rule, `ReadPipelineCacheFile`, `WritePipelineCacheFile`, moved out of
`rhi/vulkan/`), `engine/rhi/vulkan/vk_pipeline_policy.h/.cpp` and `vulkan_device_pipelines.cpp` (now calling them),
`tests/rhi/vulkan_pipeline_test.cpp` (the include), the new `tests/rhi/pipeline_cache_file_test.cpp` (phase 13, every platform),
`tools/config_access_check/allowlist.txt`, `engine/rhi/CMakeLists.txt`, the guide, the spec, the reference and the lint baseline. Run: both
unity modes, `--affected` with asan and tsan, the Vulkan pipeline cache tests, `ctest -L docs`.

## Runs

Windows PC:

- `scripts/test-phase.sh --affected`: `ran 1246 of 2436 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 136 s; the last full run took 13 s`.
- `scripts/test-phase.sh --affected --flavor asan`: `ran 925 of 2436 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 69 s; the last full run took 13 s`.
- `rhi_tests "[dx12]"`: 28 test cases, the debug layer silent; `"[pipeline_cache]"`: 3 test cases. All three trees build clean;
  `cmake --build build-msvc --target docs`: 51 samples, 0 failed; lint 4409 of 4409 documented; `ctest -L docs` 6 of 6.
- Linux: not run; pending until the phase close (Day 174).

## Post-implementation code review

`/code-review high` on the day's diff: 6 findings, 3 fixed, 3 declined. Found and fixed while writing the day, before the review: the
library's lookup misses made the debug layer warn (the name list), and pipelines compiled under the resource lock (the shared shader bytes).

- **Fixed (regression test, fails without the fix):** `ReadPipelineCacheFile` left the output's old bytes, or a partial read, in place on
  failure, and both backends take a non-empty output as a file read; it empties the output now ("A failed pipeline cache read leaves the
  output empty, whatever it held").
- **Fixed:** the spec's root-parameter order did not match the code (amended, above).
- **Fixed:** `PipelineCacheStats` said pipelines where it counts pipeline states.
- **Declined:** a 64-bit key collision between two pipelines would make one of them compile every run (still correct); not worth a
  second check.
- **Declined:** a restart strip compiles both cut values up front; one pipeline of the engine is one, in a test.
- **Declined (filed):** the cold measurement ran with the driver's shader cache warm; emptying it is a change to the machine.
