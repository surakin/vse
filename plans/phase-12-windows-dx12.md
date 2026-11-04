# Phase 12 — Windows Port + DX12 Backend

**Depends on:** Phases 0-11 all working on Linux — this phase ports the
whole stack built so far, it doesn't build new engine features. Unreal
import (Phases 16-17) is deliberately sequenced *after* multiplatform
support, so it isn't part of what this phase ports.
**Goal:** everything built so far running on Windows, with DX12 as the
RHI backend (Vulkan also available on Windows if useful, but DX12 is the
native target).
**Definition of done:** Phases 1-11's functionality (window/input, RHI,
renderer, ECS, physics, audio, asset pipeline, scripting) all working on
Windows through the DX12 RHI backend.

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

- Full day-by-day breakdown deferred until Phase 11 is complete.
- Windows' lack of a raw "syscall" layer the way Linux has one means the
  no-CRT boundary is drawn differently here — Win32 itself is the
  platform contract. Document the precise boundary when this phase starts.

## Shader toolchain (from PLAN.md assumption #18)

The backend needs DXIL shaders. The toolchain that produces them from the
engine's shader source was to be settled before Phase 4; confirm that
decision here and that the cooker emits DXIL blobs for `ShaderDesc`
before this phase's day files are written.
