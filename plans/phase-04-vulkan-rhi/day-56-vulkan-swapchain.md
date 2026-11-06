# Phase 4 / Day 56 — Swapchain & Window Presentation

**Phase:** [Phase 4 — Vulkan RHI](../phase-04-vulkan-rhi.md)
**Depends on:** Days 48, 55.

Presenting to the real Phase 1 window, on Wayland and X11.

## Tasks

- [x] Surface creation from `os::NativeWindowInfo`: `vkCreateWaylandSurfaceKHR` or `vkCreateXlibSurfaceKHR` by `kind`; unsupported kind → clear error; the surface is destroyed before the instance.
- [x] Swapchain creation policy as pure, tested functions over plain data: surface format choice (prefer `B8G8R8A8_SRGB`/`R8G8B8A8_SRGB` with sRGB-nonlinear, fall back with a log), present mode (FIFO default, MAILBOX when requested and available, IMMEDIATE only on request), image count (min+1 capped by max), extent from the surface capabilities (handling the "undefined extent" case on Wayland from `Window::Width/Height`), pre-transform and composite alpha.
- [x] `Acquire()`: `vkAcquireNextImageKHR` with the frame's binary semaphore; `OUT_OF_DATE` → recreate and retry once; `SUBOPTIMAL` → recreate after presenting. `Present()`: waits on the render-finished semaphore; same recovery. Returns a status enum (`Ok`, `Recreated`, `Minimized`, `Lost`).
- [x] `Resize`/recreation: on `Window::ResizeSerial()` change or OUT_OF_DATE, `WaitIdle` (or retire the old swapchain with `oldSwapchain` + deferred destroy), recreate images and views; extent 0×0 / `IsMinimized()` → skip rendering and return `Minimized` without creating a zero-sized swapchain.
- [x] Swapchain images exposed as `TextureHandle`s with `ResourceState::Present`↔`ColorAttachment` barriers; `CurrentTexture()`.
- [x] Add `vkCreateXlibSurfaceKHR` (deliberately left out of the Day 49 function tables: `vulkan_xlib.h` includes `<X11/Xlib.h>`, whose `Success`/`None`/`Bool` macros break enums like `VkResultClass::Success`). Declare the PFN and the `VkXlibSurfaceCreateInfoKHR` ABI mirror (or include the header from one isolated translation unit) so the macros never reach the rest of the backend.
- [x] Wayland has no client-visible minimized/occluded state (`Window::IsMinimized()` only sees zero area), and a hidden surface stops receiving frame callbacks, so a FIFO present can stall: never block indefinitely in `Acquire`/`Present` (bounded timeouts, return `Minimized` on timeout) and record what was observed on the dev compositor.
- [x] Tests: format/present-mode/count/extent selection functions with synthetic capability tables; `[gpu][window]`: create a window on the active backend, make a swapchain, acquire/present 10 frames, resize the window programmatically twice (swapchain recreates, extent matches), iconify path returns `Minimized`; run under Wayland and X11 (`ENGINE_WINDOW_BACKEND`), skipped when the display is absent.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** swapchain APIs and their status codes documented; guide section "Presenting to a window" with the minimize/resize rules.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** a swapchain presents to both Wayland and X11 windows, survives resize and minimize, and its policy functions are unit-tested.

## Result

`DeviceCreateInfo::window` makes the Vulkan device present-capable: the instance enables the surface extensions, a surface is made from the window's `NativeWindowInfo` (Wayland through the function table; Xlib through `vk_xlib_surface.cpp`, the one translation unit that includes the X11 headers, so `Success`/`None`/`Bool` never reach the backend), the GPU must be able to present to it, and `VK_KHR_swapchain` is enabled. `Device::CreateSwapchain/DestroySwapchain` return a `VulkanSwapchain`: pure policy functions in `vk_swapchain_policy.h` (format, present mode, image count, extent, composite alpha, usage), images exposed as device textures, `Acquire`/`Present` that carry the binary semaphores in empty internal batches so the application's lists use only the timeline fence, recreation on resize serial / out of date, `Minimized` handling, and a half-second bounded acquire. The null backend has a rotating three-image swapchain. Tests: policy tables (formats, modes, counts, extents including Wayland's undefined extent, alpha, usage), null swapchain, and `[gpu][window]` presentation on Wayland and X11 (10 frames, two resizes, minimize and recovery) with validation and sync validation clean. Docs 1590/1590; guide section "Presenting to a window".

Observed on the dev machine (NVIDIA driver, Wayland compositor `wayland-1` with XWayland `:0`): both windows present without stalling; the Wayland surface reports an undefined extent so the swapchain takes the size given to `Resize`/the window size, the X11 surface fixes the extent to the window. Not exercised: a Wayland surface the compositor stopped drawing (the half-second `Acquire` bound handles it by design), and a programmatic window resize or iconify, because the platform window has no such API yet; the resize and minimize paths are driven through `Swapchain::Resize` and zero sizes instead.

Deviations from the plan: `Present` reports `Recreated` for an out-of-date surface and rebuilds on the next `Acquire`, not immediately, because rebuilding would destroy the semaphore the present just queued. The ASan flavor skips the X11 presentation test: the NVIDIA driver's Xlib presentation path leaks inside a library it unloads before exit (every report frame is an anonymous module, so no suppression can name it), while the Wayland test, which runs the same code of ours, is clean.

## Post-implementation code review

`/code-review high` returned 10 findings.

Fixed:
2. `Present` rebuilt the swapchain right after queueing a present that waits on a semaphore the rebuild destroys: the rebuild is deferred to the next `Acquire`.
3. The null swapchain had no lifetime tracking: one per device (a second is an error), destroying the device frees a forgotten one with an error; regression test.
4. A persistent `SUBOPTIMAL` rebuilt the swapchain every frame: suboptimal results are now ignored, and the resize serial decides when to rebuild.
5. Image count above the fixed array: the array is 16, a surface needing more or a swapchain with more images fails cleanly instead of reading out of bounds.
7. A timeline wait failure in `Acquire` was reported as `Minimized`: it is logged and reported as `Lost`.
8. The fallback format ignored the color space: it now prefers sRGB-nonlinear; tests added.
9. `Present` could return `Lost` after presenting and `Minimized` outside its documented set: recreation no longer happens inside `Present`, and the `Recreated` doc says the image may not have been shown.
10. The null swapchain lacked the double-`Acquire` assertion: added and documented.

Declined:
1. "The acquire wait is in a separate batch, so later batches are not ordered after it": the Vulkan spec makes the second synchronization scope of a semaphore wait include every command later in submission order (limited to the wait's stage mask, which is `ALL_COMMANDS` here), and sync validation reports nothing across the presentation tests.
6. "Surface errors mark the whole device lost": `NoteDeviceLost` reacts only to `VK_ERROR_DEVICE_LOST`; surface and swapchain errors set the swapchain's own lost state.
