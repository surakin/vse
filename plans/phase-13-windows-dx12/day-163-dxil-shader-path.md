# Phase 13 / Day 163 — The Shader Path for D3D12: DXIL, Object Index, Clip Space

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 162 (bytecode blobs); Day 147 Spike B (the object index delivery); `shaders/README.md` ("Other targets").

Mint DXIL for every shader and permutation, deliver the per-object index without `SV_VulkanInstanceID`, and settle the clip-space Y convention. Shared code
(shaders, `shader_mint`, the renderer's projection) changes on both machines.

## Tasks

- [ ] **Tool discovery** (same rule as Day 148's compilers: found by the build, no developer prompt): `slangc` from `ENGINE_SLANGC`, `PATH` or the recorded
      install location; `dxc.exe`, `dxcompiler.dll` and `dxil.dll` from the Windows SDK found by Day 148's toolchain (`bin/<version>/x64`), or `ENGINE_DXC`, never the
      first `dxc` on `PATH` (the Vulkan SDK puts an older one there: 1.9.0.5512 against the SDK's 1.9.2602.17, spec section 1); the version used is recorded
      beside the committed DXIL. On Linux `dxc` stays optional. Both stay optional for a checkout: without them the build uses the committed outputs.
- [ ] **`shader_mint --permute` on Windows:** the `posix_spawnp` start of `slangc` goes behind a small process helper (`CreateProcessW` on Windows, the existing
      spawn on Linux) with the same argument quoting tests on both; `KNOWN-ISSUES.md` entry removed. `tools/shader_mint` (and its tests) build on
      Windows from today (moved from Day 150) and get `ENGINE_EXCEPTIONS_COMPILE_OPTIONS`: nlohmann/json throws, and the engine's `/EHs-c-` would
      reject it (Day 148 review).
- [ ] **DXIL minting:** `slangc -target dxil -profile sm_6_0` (the spec's shader model) with `-matrix-layout-column-major` and the existing flags, signed/validated
      DXIL (`dxil.dll` present so the output is validator-signed; record what `slangc` needs for that); outputs committed as `shaders/*.dxil.inc` for every shader
      and every permutation key, beside the `.spv.inc`; the reflection (`*.refl.inc`) gains the D3D12 register and space of each binding where it differs.
- [ ] **Determinism:** the mint is byte-identical between two runs and between the Windows PC and the Linux machine (same `slangc` version, LF outputs); the
      existing determinism ctest covers DXIL; a mismatch between machines is a failure, not a re-mint.
- [ ] **Object index delivery** (decided on Day 147, Spike B; spec decision 9): every material vertex shader (`gbuffer.vert`, `lit.vert`, `cube.vert`, the
      permuted ones) reads the object index from an instance-rate `OBJECT_INDEX` attribute behind `ENGINE_OBJECT_INDEX_STREAM` on D3D12, fed by one static
      index buffer `[0, N)` at vertex-buffer slot 1 that the renderer binds once per pass; `GBufferPass` keeps drawing object `i` with `first_instance = i`
      (D3D12 fetches instance data at `StartInstanceLocation + SV_InstanceID`), so neither it nor the RHI interface changes. Decide whether the SPIR-V
      build uses the same stream (one shader form on every target, which Metal also wants) or keeps `SV_VulkanInstanceID`; measure the Vulkan draw cost both
      ways before switching. Vulkan golden images byte-identical on Linux before and after. `KNOWN-ISSUES.md` entries for the HLSL index removed (the Metal half stays for Phase 14).
- [ ] **Clip space:** `PerspectiveD3D` (or a per-backend flag on the camera's projection) with Y up and depth 0..1; which one the renderer uses comes from
      `DeviceCaps` or the backend descriptor, never from an `#ifdef`; tests of both projections against hand-computed matrices; `KNOWN-ISSUES.md` Y-flip entry
      updated (Metal remains).
- [ ] **`shaders_portability`** runs its DXIL step with the SDK's `dxc` on Windows and passes (the Linux `dxc` abort stays recorded in `KNOWN-ISSUES.md` for
      Linux only); the `shaders_*` ctests that compare committed outputs pass on both machines.
- [ ] **Documentation:** `shaders/README.md` (DXIL flags, the object index define, the projection rule, Windows tool discovery); `docs/guide/rhi-shaders.md`
      "DXIL"; `docs/guide/renderer.md` projection note; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] **Runs:** Windows and Linux `--affected` (shaders and renderer: everything in phases 4-5) plus each machine's sanitizer flavors; quote all.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: the index stream's slot colliding with a mesh's own vertex buffers, a pass that forgets to bind it, the projection
      choice reaching every pass (including the fullscreen and debug passes), and process-start argument quoting.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every shader and permutation has committed, deterministic DXIL minted identically on both machines, material shaders get their object index on D3D12
without `SV_VulkanInstanceID`, the projection is chosen per backend, and Vulkan output is unchanged.
