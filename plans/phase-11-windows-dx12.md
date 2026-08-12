# Phase 11 — Windows Port + DX12 Backend

**Depends on:** Phases 0-10 all working on Linux — this phase ports the
whole stack, it doesn't build new engine features.
**Goal:** everything built so far running on Windows, with DX12 as the
RHI backend (Vulkan also available on Windows if useful, but DX12 is the
native target).
**Definition of done:** Phases 1-10's functionality (window/input, RHI,
renderer, ECS, physics, audio, asset pipeline, Unreal import, scripting)
all working on Windows through the DX12 RHI backend.

## Key deliverables (to be expanded into day-by-day tasks when started)

- Windows platform layer: raw Win32 API calls (window creation, message
  loop, raw input) — no CRT, matching the Linux platform layer's
  no-dependency stance; Windows x64 calling convention/ABI differs from
  System V and needs its own syscall-adjacent groundwork (Win32 isn't a
  raw syscall interface the way Linux is — it's the OS boundary itself,
  consistent with assumption #1).
- DX12 RHI backend implementing the same interface Phase 2 defined for
  Vulkan.
- Build system support for cross-compiling/targeting Windows from the
  existing toolchain, or a native Windows build path — decide when
  reached.

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 10 is complete.
- Windows' lack of a raw "syscall" layer the way Linux has one means the
  no-CRT boundary is drawn differently here — Win32 itself is the
  platform contract. Document the precise boundary when this phase starts.
