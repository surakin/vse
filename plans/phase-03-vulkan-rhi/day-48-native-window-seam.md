# Phase 3 / Day 48 — Native Window Handle Seam (Platform Layer)

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 47; Phase 1 window code.

The one change Phase 3 needs in Phase 1: a platform-neutral way to hand the swapchain the native window, without the RHI including Wayland/X11 headers.

## Tasks

- [ ] Add `os::NativeWindowInfo` to `engine/foundation/os/window.h`: a tagged struct (`kind`: `None`, `Wayland`, `Xlib`, reserved `Win32`, `Cocoa`, `Android`; two opaque `void*` fields, e.g. `display`/`surface` for Wayland, `Display*`/`Window` as an integer for Xlib) plus `Window::GetNativeInfo()` (pure virtual with a default implementation returning `None` is **not** allowed — every backend must answer).
- [ ] Implement it in `WaylandWindow`/`WaylandPlatformWindow` (the `wl_display*` and `wl_surface*` the window owns) and `X11Window` (`Display*`, `Window` id); document ownership: the pointers are borrowed, valid until the window is destroyed.
- [ ] Add a resize/minimize signal the swapchain needs: `Window::Width/Height` already exist — add `Window::IsMinimized()` (zero-area or iconified) and a monotonically increasing `Window::ResizeSerial()` so a swapchain can cheaply detect "extent changed since I was created". Implement for both backends.
- [ ] Keep the seam future-proof per assumption #11: no Vulkan, Wayland or X11 types in the public header; fields documented per future platform in the spec.
- [ ] Tests (existing window test targets): both backends return the right `kind` and non-null handles for a live window; `ResizeSerial` bumps on resize; `IsMinimized` is false for a mapped window. Existing tests unchanged and green.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the new `os::` symbols are documented (Ownership of the borrowed handles; Thread-safety `owner-thread-only`) and `docs/guide/platform-windowing-and-input.md` gains a "Native handles" section.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** both window backends expose their native handles, minimized state and resize serial through the portable interface, with no new platform types leaking upward.
