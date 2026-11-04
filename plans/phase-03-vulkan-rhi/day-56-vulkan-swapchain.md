# Phase 3 / Day 56 — Swapchain & Window Presentation

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Days 48, 55.

Presenting to the real Phase 1 window, on Wayland and X11.

## Tasks

- [ ] Surface creation from `os::NativeWindowInfo`: `vkCreateWaylandSurfaceKHR` or `vkCreateXlibSurfaceKHR` by `kind`; unsupported kind → clear error; the surface is destroyed before the instance.
- [ ] Swapchain creation policy as pure, tested functions over plain data: surface format choice (prefer `B8G8R8A8_SRGB`/`R8G8B8A8_SRGB` with sRGB-nonlinear, fall back with a log), present mode (FIFO default, MAILBOX when requested and available, IMMEDIATE only on request), image count (min+1 capped by max), extent from the surface capabilities (handling the "undefined extent" case on Wayland from `Window::Width/Height`), pre-transform and composite alpha.
- [ ] `Acquire()`: `vkAcquireNextImageKHR` with the frame's binary semaphore; `OUT_OF_DATE` → recreate and retry once; `SUBOPTIMAL` → recreate after presenting. `Present()`: waits on the render-finished semaphore; same recovery. Returns a status enum (`Ok`, `Recreated`, `Minimized`, `Lost`).
- [ ] `Resize`/recreation: on `Window::ResizeSerial()` change or OUT_OF_DATE, `WaitIdle` (or retire the old swapchain with `oldSwapchain` + deferred destroy), recreate images and views; extent 0×0 / `IsMinimized()` → skip rendering and return `Minimized` without creating a zero-sized swapchain.
- [ ] Swapchain images exposed as `TextureHandle`s with `ResourceState::Present`↔`ColorAttachment` barriers; `CurrentTexture()`.
- [ ] Add `vkCreateXlibSurfaceKHR` (deliberately left out of the Day 49 function tables: `vulkan_xlib.h` includes `<X11/Xlib.h>`, whose `Success`/`None`/`Bool` macros break enums like `VkResultClass::Success`). Declare the PFN and the `VkXlibSurfaceCreateInfoKHR` ABI mirror (or include the header from one isolated translation unit) so the macros never reach the rest of the backend.
- [ ] Wayland has no client-visible minimized/occluded state (`Window::IsMinimized()` only sees zero area), and a hidden surface stops receiving frame callbacks, so a FIFO present can stall: never block indefinitely in `Acquire`/`Present` (bounded timeouts, return `Minimized` on timeout) and record what was observed on the dev compositor.
- [ ] Tests: format/present-mode/count/extent selection functions with synthetic capability tables; `[gpu][window]`: create a window on the active backend, make a swapchain, acquire/present 10 frames, resize the window programmatically twice (swapchain recreates, extent matches), iconify path returns `Minimized`; run under Wayland and X11 (`ENGINE_WINDOW_BACKEND`), skipped when the display is absent.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** swapchain APIs and their status codes documented; guide section "Presenting to a window" with the minimize/resize rules.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** a swapchain presents to both Wayland and X11 windows, survives resize and minimize, and its policy functions are unit-tested.
