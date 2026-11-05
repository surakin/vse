# Phase 5 — Windows Port + DX12 Backend

**Depends on:** Phase 4 (Renderer core) complete on Linux — this phase ports
the stack built so far (platform layer, RHI, renderer, shaders, job system,
foundation); it doesn't build new engine features. Unreal import (Phases
16-17) is deliberately sequenced after multiplatform support, so it isn't
part of what this phase ports. Before it starts, every day of this phase
needs its own file under `plans/phase-05-windows-dx12/` (see `CLAUDE.md`,
planning policy).
**Goal:** Phases 1-4 running on Windows, with DX12 as the RHI backend
(Vulkan also available on Windows if useful, but DX12 is the native
target). Moved ahead of Font, UI, ECS and the rest on 2026-11-05 so the
RHI and platform abstractions are tested against a second backend while
they are still small.
**Definition of done:** the Phase 3 golden images (triangle, compute,
cube, lit cube) and the Phase 4 scene tests (render graph, materials,
deferred lighting, permuted shaders) pass on Windows through the DX12 RHI
backend, with the validation layers (D3D12 debug layer) silent; the job
system and foundation tests pass on Windows.
**Hardware:** the developer's Windows PC (GPU and Windows version to be
recorded here when the phase is broken into days).

## Key deliverables (to be expanded into day-by-day tasks when started)

- Windows platform layer: raw Win32 API calls (window creation, message
  loop, raw input) — no CRT, matching the Linux platform layer's
  no-dependency stance; Windows x64 calling convention/ABI differs from
  System V and needs its own syscall-adjacent groundwork (Win32 isn't a
  raw syscall interface the way Linux is — it's the OS boundary itself,
  consistent with assumption #1).
- DX12 RHI backend implementing the same interface Phase 3 defined for
  Vulkan.
- Build system support for cross-compiling/targeting Windows from the
  existing toolchain, or a native Windows build path — decide when
  reached.

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 4 is complete.
- **Open, decided at Phase 6 completion:** how Phases 7-13 keep the Windows build working (see `PLAN.md` assumption #11). Until then those phases are developed on Linux first.
- Windows' lack of a raw "syscall" layer the way Linux has one means the
  no-CRT boundary is drawn differently here — Win32 itself is the
  platform contract. Document the precise boundary when this phase starts.

## Shader toolchain (from PLAN.md assumption #18)

The backend needs DXIL shaders. The toolchain that produces them from the
engine's shader source was to be settled before Phase 4; confirm that
decision here and that the mint emits DXIL blobs for `ShaderDesc`
before this phase's day files are written.

- **From the Phase 3 portability review** (`docs/superpowers/specs/2026-11-04-rhi-portability-review.md`, section 5):
  typeless formats for sampled depth; compute-shader `GenerateMips`; bind-group descriptors copied into per-frame
  shader-visible heaps at `SetBindGroup` (measure it against the Vulkan number in the Day 67 baseline); uniform buffers
  and CBV ranges rounded to 256 bytes; CPU repacking in `UploadTexture`/`ReadbackTexture` for 256-byte pitches;
  `ResizeBuffers` after idling; sRGB render-target views over UNorm flip-model buffers; the clip-space Y flip lives in
  the shader translation path; `Mailbox` falls back to `Fifo`. Copy alignment (512 / 256) is already in
  `DeviceCaps::buffer_texture_copy_*_alignment`.

## What Phase 4 gives this phase (Phase 4 Day 84)

From the renderer (see `docs/superpowers/specs/2026-11-04-renderer-portability-review.md`; every row there was a prediction this phase tests):

- A render graph whose barriers are explicit `ResourceState` transitions: D3D12 consumes them directly (legacy `ResourceBarrier` or
  enhanced barriers); the golden images (`tests/rhi/golden/`, including `renderer_demo.ppm`) are the oracle.
- The shader path: `ctest -R shaders_portability` already compiles every shader to HLSL; this phase adds the DXIL step (`dxc`, optional in
  the test, not yet run anywhere), the `PerspectiveD3D` (Y up) projection, and the **object index delivery**: the renderer draws object
  `i` with `first_instance = i` and the shaders read `SV_VulkanInstanceID`, which D3D12 has no equivalent of; use a root constant or a
  per-draw vertex stream, behind a shader define (touches every material shader's vertex stage and `GBufferPass` recording).
- The depth sampling shim (typeless `R32_TYPELESS` resource) is an RHI-backend item, not a renderer one.
- `rhi_bench` and `renderer_bench` are the performance baselines to compare descriptor copy cost and instanced-draw cost against.
