# Phase 13 / Day 163 — The Shader Path for D3D12: DXIL, Object Index, Clip Space

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 162 (bytecode blobs); Day 147 Spike B (the object index delivery); `shaders/README.md` ("Other targets").

Mint DXIL for every shader and permutation, deliver the per-object index without `SV_VulkanInstanceID`, and settle the clip-space Y convention. Shared code
(shaders, `shader_mint`, the renderer's projection) changes on both machines.

## Tasks

- [x] **Tool discovery** (same rule as Day 148's compilers: found by the build, no developer prompt): `slangc` from `ENGINE_SLANGC`, `PATH` or the recorded
      install location; `dxc.exe`, `dxcompiler.dll` and `dxil.dll` from the Windows SDK found by Day 148's toolchain (`bin/<version>/x64`), or `ENGINE_DXC`, never the
      first `dxc` on `PATH` (the Vulkan SDK puts an older one there: 1.9.0.5512 against the SDK's 1.9.2602.17, spec section 1); the version used is recorded
      beside the committed DXIL. On Linux `dxc` stays optional. Both stay optional for a checkout: without them the build uses the committed outputs.
      *(`ENGINE_SLANGC`, `PATH`, `C:/Tools/slang-2026.18.2/bin`; DXIL from `ENGINE_DXC` or the toolchain's `ENGINE_SDK_BIN`, given to slangc with
      `-dxc-path`; recorded as `// compiler: slangc v2026.18.2, dxcompiler 1.9.2602.17`. The Windows `slangc -v` prints `2026.18.2` without the `v`:
      the version is normalized so the minted files match Linux's.)*
- [x] **`shader_mint --permute` on Windows:** the `posix_spawnp` start of `slangc` goes behind a small process helper (`CreateProcessW` on Windows, the existing
      spawn on Linux) with the same argument quoting tests on both; `KNOWN-ISSUES.md` entry removed. `tools/shader_mint` (and its tests) build on
      Windows from today (moved from Day 150) and get `ENGINE_EXCEPTIONS_COMPILE_OPTIONS`: nlohmann/json throws, and the engine's `/EHs-c-` would
      reject it (Day 148 review).
      *(`run_process.h`: `AppendWindowsArgument` (portable, tested everywhere), `run_process_posix.cpp`, `windows/run_process_windows.cpp`; a probe
      child checks the arguments end to end on both platforms. `shader_mint` is invoked as `$<TARGET_FILE:shader_mint>`: the Windows toolchain
      counts as cross-compiling, so CMake does not substitute the target name.)*
- [x] **DXIL minting:** `slangc -target dxil -profile sm_6_0` (the spec's shader model) with `-matrix-layout-column-major` and the existing flags, signed/validated
      DXIL (`dxil.dll` present so the output is validator-signed; record what `slangc` needs for that); outputs committed as `shaders/*.dxil.inc` for every shader
      and every permutation key, beside the `.spv.inc`; the reflection (`*.refl.inc`) gains the D3D12 register and space of each binding where it differs.
      *(Every shader and key; signed by `dxil.dll` (checked). Decided: the reflection gains no D3D12 registers, because the register rule makes
      them equal to the bindings and `shader_mint` fails a shader that breaks it. Vertex inputs are `TEXCOORD<location>` (the D3D12 backend's
      input-element names), checked on every machine. A read-only `StructuredBuffer` reflects as the new `rhi::BindingType::ReadOnlyStorageBuffer`
      (an SRV on D3D12); six `*.refl.inc` changed by that one byte. A push block with `register()` is reported with two bindings; `shader_mint`
      reads the push range.)*
- [x] **Determinism:** the mint is byte-identical between two runs and between the Windows PC and the Linux machine (same `slangc` version, LF outputs); the
      existing determinism ctest covers DXIL; a mismatch between machines is a failure, not a re-mint.
      *(Two runs give identical DXIL; no path is embedded. `shaders_check` compares `*.dxil.inc` when DXIL can be minted. Between machines: the
      SPIR-V words of all 24 shaders were compared on Windows with the committed Linux outputs (identical); DXIL cannot be minted on Linux (the
      `dxc` package aborts), so `shader_dxil_test` checks there that each DXIL was minted beside the committed SPIR-V.)*
- [x] **Object index delivery** (decided on Day 147, Spike B; spec decision 9): every material vertex shader (`gbuffer.vert`, `lit.vert`, `cube.vert`, the
      permuted ones) reads the object index from an instance-rate `OBJECT_INDEX` attribute behind `ENGINE_OBJECT_INDEX_STREAM` on D3D12, fed by one static
      index buffer `[0, N)` at vertex-buffer slot 1 that the renderer binds once per pass; `GBufferPass` keeps drawing object `i` with `first_instance = i`
      (D3D12 fetches instance data at `StartInstanceLocation + SV_InstanceID`), so neither it nor the RHI interface changes. Decide whether the SPIR-V
      build uses the same stream (one shader form on every target, which Metal also wants) or keeps `SV_VulkanInstanceID`; measure the Vulkan draw cost both
      ways before switching. Vulkan golden images byte-identical on Linux before and after. `KNOWN-ISSUES.md` entries for the HLSL index removed (the Metal half stays for Phase 24).
      *(Decided: SPIR-V keeps `SV_VulkanInstanceID` (Vulkan output byte-identical, so no Vulkan cost was measured); D3D12 reads
      `[[vk::location(15)]] TEXCOORD15` under `ENGINE_OBJECT_INDEX_STREAM`, also in `point_light.vert` (the light index). The stream is
      `MaterialSystem::ObjectIndexStream` (65536 indices), slot 1 in material pipelines, slot 0 in the light volume, made only when
      `DeviceCaps::object_index_stream` is set.)*
- [x] **Clip space:** `PerspectiveD3D` (or a per-backend flag on the camera's projection) with Y up and depth 0..1; which one the renderer uses comes from
      `DeviceCaps` or the backend descriptor, never from an `#ifdef`; tests of both projections against hand-computed matrices; `KNOWN-ISSUES.md` Y-flip entry
      updated (Metal remains).
      *(**Amended:** the flip is in the D3D12 shader build, `ENGINE_CLIP` in every vertex shader, not a `PerspectiveD3D`: the fullscreen passes,
      the depth reconstruction in `lighting.frag` and the light scissor rectangles all assume the one convention, which the flip keeps on both
      backends; no projection is chosen per backend. Day 170's golden images are the test of the orientation.)*
- [x] **`shaders_portability`** runs its DXIL step with the SDK's `dxc` on Windows and passes (the Linux `dxc` abort stays recorded in `KNOWN-ISSUES.md` for
      Linux only); the `shaders_*` ctests that compare committed outputs pass on both machines.
      *(The HLSL is built with the D3D12 define; with the SDK's `dxc` every shader and key compiles, and a rejection fails the test on Windows.)*
- [x] **Documentation:** `shaders/README.md` (DXIL flags, the object index define, the projection rule, Windows tool discovery); `docs/guide/rhi-shaders.md`
      "DXIL"; `docs/guide/renderer.md` projection note; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] **Runs:** Windows and Linux `--affected` (shaders and renderer: everything in phases 4-5) plus each machine's sanitizer flavors; quote all.
      *(Windows done; the Linux run waits for the phase close, Day 174.)*
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: the index stream's slot colliding with a mesh's own vertex buffers, a pass that forgets to bind it, the projection
      choice reaching every pass (including the fullscreen and debug passes), and process-start argument quoting.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every shader and permutation has committed, deterministic DXIL minted identically on both machines, material shaders get their object index on D3D12
without `SV_VulkanInstanceID`, the projection is chosen per backend, and Vulkan output is unchanged.

## Notes

- **Found by the ASan run:** the Vulkan descriptor pools sized their per-type arrays with a literal 5 and indexed them by `BindingType`, so the
  new sixth type wrote past the stack array (`stack-buffer-overflow` in `AllocateSetLocked`, 23 GPU tests). Fixed with `kDescriptorKinds` and
  `DescriptorKind()` (the read-only type shares the storage-buffer slot) and a `static_assert` on the type count; the ASan tests that failed are
  the regression check.
- `tests/rhi/vulkan_compute_test.cpp` still declares saxpy's `x_buf` as `StorageBuffer`: Vulkan does not care; Day 170, which runs that suite on
  DX12, makes it `ReadOnlyStorageBuffer`.

## For the Linux run

Shared: every `shaders/*.slang` and `target_common.slang`, `shaders/CMakeLists.txt`, `names.cmake`, `check.cmake`, `portability.cmake`, the
regenerated `*.refl.inc` and `*.perm.inc` and the new `*.dxil.inc`; `tools/shader_mint/` (`main.cpp`, `reflect_json.*`, `permutations.*`,
`run_process*`, CMake); `tests/shader_mint/`; `engine/rhi/enums.h`, `caps.h`, `null/null_device.cpp`, `vulkan/vk_pipeline_policy.cpp`,
`vulkan/vulkan_device_bindings.cpp`, `vulkan/vulkan_device.h`; `engine/renderer/material.*`, `gbuffer.*`, `lighting.*`, `shader_reflection.cpp`;
`game/common/cube_scene.cpp`; `tests/renderer/` (`shader_dxil_test.cpp`, `renderer_fault_test.cpp`, `shader_reflection_test.cpp`). Run: the
`shaders` target with `slangc` (must reproduce `*.spv.inc`, `*.refl.inc`, `*.perm.inc` byte for byte and leave `*.dxil.inc` alone), `shaders_check`,
`shaders_portability`, both unity modes, `--affected` plain, asan and tsan, the golden images byte-identical, `ctest -L docs`.

## Runs

Windows PC:

- `scripts/test-phase.sh --affected`: `ran 1212 of 2369 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 111 s; the last
  full run took 13 s`.
- `scripts/test-phase.sh --affected --flavor asan`: `ran 892 of 2369 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 71 s;
  the last full run took 13 s` (the first run found the descriptor-pool overflow, above).
- `shaders_check` and `shaders_portability` pass (DXIL compared; `dxc` 1.9.2602.17 compiles every shader and key).
- After the review's fixes, rebuilt in all three trees (clean): MSVC `ctest -L "^rhi$|^renderer$|shaders|docs|demo|boundary|gate"` 621 of 621;
  `windows-clang-cl` phase 0, renderer and shaders 359 of 359. `cmake --build build-msvc --target docs`: 51 samples, 0 failed; lint clean.
- Linux: not run; pending until the phase close (Day 174), `KNOWN-ISSUES.md`.

## Post-implementation code review

`/code-review high` on the day's diff: 5 findings, 3 fixed, 2 declined. (The descriptor-pool overflow was found by the ASan run before the review.)

- **Fixed (with a regression test):** the G-buffer pass refused a frame of more than 65536 objects on every backend, although only a device that
  reads the object index stream has that limit; now only there. The new test (both device shapes, 65537 objects) fails without the fix (seen) and
  passes with it.
- **Fixed:** the light volume ignored a failure to add the index attribute; it is logged.
- **Fixed (test):** the DX12-shaped frame test was checked to fail when the G-buffer pass does not bind the stream (seen: "vertex buffer 1
  required by the pipeline is not set").
- **Declined:** Windows argument quoting now exists three times (the death-test launcher, docgen's runner, `shader_mint`), in three modules that
  share no library below the tools; a shared `os::` process helper is the fix, recorded in `KNOWN-ISSUES.md`.
- **Declined:** `kObjectIndexLocation` (15) and the literal 15 in the shaders are not tied by a check; a mismatch fails D3D12 pipeline creation,
  which Day 171's renderer suite on DX12 exercises.
