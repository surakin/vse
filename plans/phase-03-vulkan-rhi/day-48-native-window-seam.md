# Phase 3 / Day 48 — Native Window Handle Seam (Platform Layer)

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 47; Phase 1 window code.

The one change Phase 3 needs in Phase 1: a platform-neutral way to hand the swapchain the native window, without the RHI including Wayland/X11 headers.

## Tasks

- [x] Add `os::NativeWindowInfo` to `engine/foundation/os/window.h`: a tagged struct (`kind`: `Invalid` (not `None`, which the X11 headers define as a macro), `Wayland`, `Xlib`, reserved `Win32`, `Cocoa`, `Android`; two opaque `void*` fields, e.g. `display`/`surface` for Wayland, `Display*`/`Window` as an integer for Xlib) plus `Window::GetNativeInfo()` (pure virtual with a default implementation returning `None` is **not** allowed — every backend must answer).
- [x] Implement it in `WaylandWindow`/`WaylandPlatformWindow` (the `wl_display*` and `wl_surface*` the window owns) and `X11Window` (`Display*`, `Window` id); document ownership: the pointers are borrowed, valid until the window is destroyed.
- [x] Add a resize/minimize signal the swapchain needs: `Window::Width/Height` already exist — add `Window::IsMinimized()` (zero-area or iconified) and a monotonically increasing `Window::ResizeSerial()` so a swapchain can cheaply detect "extent changed since I was created". Implement for both backends.
- [x] Keep the seam future-proof per assumption #11: no Vulkan, Wayland or X11 types in the public header; fields documented per future platform in the spec.
- [x] Tests (existing window test targets): both backends return the right `kind` and non-null handles for a live window; `ResizeSerial` bumps on resize; `IsMinimized` is false for a mapped window. Existing tests unchanged and green.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the new `os::` symbols are documented (Ownership of the borrowed handles; Thread-safety `owner-thread-only`) and `docs/guide/platform-windowing-and-input.md` gains a "Native handles" section.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** both window backends expose their native handles, minimized state and resize serial through the portable interface, with no new platform types leaking upward.

## Result

`os::NativeWindowInfo`/`NativeWindowKind`, `Window::GetNativeInfo()`, `IsMinimized()` and `ResizeSerial()` on both backends. Serials come from one process-wide counter (`os::detail::NextResizeSerial()`), so they are never 0 for an open window and never repeat across size changes, windows, or close/reopen. Docs 1207/1207, `ctest -L docs` 4/4; foundation tests pass plain and under ASan except the pre-existing `X11Window::Create opens a mapped window on the real X server` failure (fails on unmodified HEAD; the window never becomes viewable in this environment, so the iconify test uses synthetic Unmap/MapNotify events).

## Post-implementation code review

`/code-review high` on the day's diff, 7 findings:

1. **STATUS.md not updated** — fixed.
2. **X11 resize-serial test flaky** (fixed settle-count of pumps vs. window-manager configure bursts) — fixed: pump until the serial is stable for 20 consecutive pumps.
3. **`X11Window::ReleaseAll` left stale `unmapped_`/size/serial** — fixed: cleared; regression assertions in the move/close tests.
4. **Wayland `IsMinimized`/`ResizeSerial` ignored a lost connection** — fixed: both use `Valid()`; assertion added to the dead-compositor test.
5. **Per-window serial could repeat after close/reopen or across windows** — fixed with the global counter; tests cover two windows and reopen.
6. **Declined:** an unmapped/hidden-desktop X11 window reporting minimized. It is correctly "nothing to draw to"; the docs say so.
7. **Declined (documented instead):** Wayland has no client-visible occlusion signal. A note was added to Day 56 (bounded timeouts in acquire/present).
