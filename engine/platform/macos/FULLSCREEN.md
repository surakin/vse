# Fullscreen on macOS (checklist for Phase 24)

`os::Window::SupportsFullscreen`, `SetFullscreen` and `IsFullscreen` are the interface (`engine/platform/window.h`).

- Use native fullscreen: `[NSWindow toggleFullScreen:]` (the window needs `NSWindowCollectionBehaviorFullScreenPrimary`). It animates
  into a separate Space, so the request is asynchronous: `IsFullscreen` follows `windowDidEnterFullScreen` /
  `windowDidExitFullScreen`, and the new size arrives through `windowDidResize` (bump `ResizeSerial`).
- Do not use `CGDisplayCapture` or a borderless window over the menu bar; native fullscreen composites correctly with the
  `CAMetalLayer`.
- Update `CAMetalLayer.drawableSize` from the new backing size (`convertRectToBacking`) so the swapchain follows.
- `SupportsFullscreen` is true for any valid window with a title bar style mask; false for a window created borderless.
- Test: the same toggle test as Linux (`tests/foundation/window_fullscreen_test.cpp`) in the `display` resource group.
