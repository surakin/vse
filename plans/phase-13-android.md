# Phase 13 — Android Port

**Depends on:** Phase 2 (Vulkan RHI) — reuses the Vulkan backend directly;
doesn't need DX12/Metal work from Phases 11-12.
**Goal:** the engine running on Android.
**Definition of done:** the Vulkan RHI backend runs unmodified on Android;
a new platform layer handles Android's app lifecycle and touch input.

## Key deliverables (to be expanded into day-by-day tasks when started)

- Android NDK platform layer: native activity lifecycle, `ANativeWindow`
  surface for Vulkan, touch input events.
- Packaging (APK) without pulling in the standard NDK C runtime, if
  feasible — needs a concrete feasibility spike, since Android's
  native-activity glue traditionally assumes a libc (Bionic) is present;
  may require a scoped exception similar to the Win32 boundary discussion
  in Phase 11.

## Notes / open questions

- Full day-by-day breakdown deferred until reached.
- Bionic libc's role on Android may force a narrower interpretation of
  "no CRT" than Linux desktop gets — decide explicitly when this phase
  starts rather than assuming it carries over unchanged.
