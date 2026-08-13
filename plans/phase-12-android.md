# Phase 12 — Android Port

**Depends on:** Phase 3 (Vulkan RHI) — reuses the Vulkan backend directly;
doesn't need DX12/Metal work from Phases 10-11.
**Goal:** the engine running on Android.
**Definition of done:** the Vulkan RHI backend runs unmodified on Android;
a new platform layer handles Android's app lifecycle and touch input.

## Key deliverables (to be expanded into day-by-day tasks when started)

- Android NDK platform layer: native activity lifecycle, `ANativeWindow`
  surface for Vulkan, touch input events.
- APK packaging via the NDK's native-activity glue, which assumes Bionic
  (Android's libc) is present — no longer a concern to route around, since
  linking the OS/distro's own libc is in scope (see root `PLAN.md`
  assumption #2). Foundation-layer code still avoids relying on it for its
  own allocators/containers/etc., same as on Linux desktop.

## Notes / open questions

- Full day-by-day breakdown deferred until reached.
