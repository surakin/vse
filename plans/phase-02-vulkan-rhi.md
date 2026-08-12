# Phase 2 — Vulkan RHI (Linux backend)

**Depends on:** Phase 1 (Linux platform layer).
**Goal:** the backend-agnostic Render Hardware Interface, with Vulkan as
its first implementation. Everything above this layer (renderer, ECS,
etc.) only ever calls the RHI, never Vulkan directly.
**Definition of done:** the RHI renders a lit, textured cube through
Vulkan on Linux; grepping the codebase outside `/engine/rhi/vulkan` turns
up zero direct Vulkan API calls.

## Immediate first step

Resolve the open risk carried over from Phase 1: does
`VK_KHR_wayland_surface`/`VK_KHR_xcb_surface` require linking
`libwayland-client`/`libxcb`, or can a valid surface be constructed from
our raw-protocol connection state? Spike this before designing the
swapchain module — it affects how "zero dependency" is scoped for display
integration going forward (see root `PLAN.md` assumption #3).

## Key deliverables (to be expanded into day-by-day tasks when started)

- Backend-agnostic RHI interface: device, buffer, texture, pipeline,
  command list, fence/semaphore abstractions — designed generically enough
  to also host DX12 and Metal later without leaking Vulkan concepts.
- Vulkan instance/physical device selection/logical device/queue setup.
- Swapchain bound to the Phase 1 window (Wayland primary, X11 fallback).
- Command buffer recording/submission, descriptor sets, pipeline state
  objects.
- Resource upload path (staging buffers → device-local memory).
- Minimal shader input: hand-authored SPIR-V modules (no GLSL/HLSL
  compiler yet — see root `PLAN.md` assumption #4).

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 1 is complete.
- Validation/debugging strategy (Vulkan validation layers) needs a decision
  — they're development-only tooling, not shipped, so likely fine to use
  even under a strict dependency policy; confirm scope when this phase
  starts.
