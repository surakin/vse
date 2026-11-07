# Platform: Windowing and Input

> **Status:** skeleton — not yet written  
> **Audience:** engine and game programmers  
> **Covers:** Phase 1


## Where the code lives, and what to link

Windows and input are the **platform layer** (layer 9): `engine/platform/`, the CMake library `platform` (`platform_asan`, `platform_tsan`), headers included as
`platform/window.h`, `platform/input.h` and `platform/window_host.h`. The types keep the `os::` namespace. `platform` is the only library that links the display libraries
(Wayland, xkbcommon, X11 on Linux); a program or test that opens a window links `platform` (the RHI libraries do, so anything on the RHI gets it). A program includes only the neutral
`platform/window_host.h`: `os::PlatformWindowHost` picks and owns the backend, which is defined in `platform/linux/` and never included by a program (the `module_deps_check` gate fails on it).

## Source of truth

- `engine/platform/window.h`
- `input.h`
- `linux/platform_window_host.h`
- `linux/wayland/*`
- `linux/x11/*`
- `linux/xkb_keyboard.h`

## API reference

Full reference: [Platform: Windowing and Input](../reference/platform-window-input.md).

- [[os::Window]] — the window interface
- [[os::WindowBackend]] — backend selection (Wayland, X11)
- [[os::InputState]] — per-window input state
- [[os::Key]] — keyboard keys
- [[os::NativeWindowInfo]] and [[os::NativeWindowKind]] — the native handles a graphics layer needs

## Already known — seed material (verify against the code when writing)

- `os::Window` interface; backend chosen by `--backend=` > `ENGINE_WINDOW_BACKEND` > auto (Wayland if WAYLAND_DISPLAY, else X11); a forced backend never falls back
- Wayland uses xdg-shell and server-side decorations (xdg-decoration); resize commits a new buffer
- Polled InputState: keyboard (layout-aware Key enum via libxkbcommon), mouse position/buttons/scroll, modifiers
- Closing is reported through ShouldClose()
- hello-window flags: `--run-seconds`, `--backend`, `--frame-times`, `--log-level`

## Outline

### The Window interface and lifecycle

_TODO_

### Choosing a backend

_TODO_

### Event pumping and the frame loop

_TODO_

### Reading input (keys, text, mouse, modifiers)

_TODO_

### Resize, close, focus

_TODO_

### Native handles, minimized state and the resize serial

A graphics layer needs three things from a window that the polled interface does not otherwise give it, and
`os::Window` answers them without exposing any platform type:

- `GetNativeInfo()` returns a plain [[os::NativeWindowInfo]]: a `kind` (Wayland, Xlib, and reserved
  Win32, Cocoa and Android values) and the handles that kind uses. For Wayland that is the `wl_display*`
  and `wl_surface*`; for X11 the `Display*` and the integer window id. A window that is not open reports
  `NativeWindowKind::Invalid`. The pointers are borrowed from the window: create the presentation surface
  after the window, and destroy it before the window.
- `IsMinimized()` is true when there is nothing to draw to: the window has zero width or height, or (X11) the
  window manager has unmapped or iconified it. Wayland does not tell clients about minimizing, so there only a
  zero-area window counts. Skip rendering while it is true.
- `ResizeSerial()` is a counter that changes each time the size changes. Remember the value when you create
  swapchain images, and recreate them when it differs. Compare it for equality only.

```cpp compile standalone run
#include <cstdio>

#include "platform/window.h"

using namespace engine_placeholder::os;

// What a presentation layer does with the answer. The enumerators are the only thing a caller needs to know
// about the platform, and the surface-creation call differs per kind.
const char* SurfaceCall(const NativeWindowInfo& info) {
    switch (info.kind) {
    case NativeWindowKind::Wayland: return "vkCreateWaylandSurfaceKHR(display, surface)";
    case NativeWindowKind::Xlib: return "vkCreateXlibSurfaceKHR(display, window_id)";
    default: return "no surface can be created";
    }
}

int main() {
    NativeWindowInfo wayland;
    wayland.kind = NativeWindowKind::Wayland;
    NativeWindowInfo xlib;
    xlib.kind = NativeWindowKind::Xlib;
    NativeWindowInfo closed;
    std::printf("%s\n%s\n%s\n", SurfaceCall(wayland), SurfaceCall(xlib), SurfaceCall(closed));
}
```

```output
vkCreateWaylandSurfaceKHR(display, surface)
vkCreateXlibSurfaceKHR(display, window_id)
no surface can be created
```

### Fullscreen

[[os::Window::SupportsFullscreen]], [[os::Window::SetFullscreen]] and [[os::Window::IsFullscreen]] ask for and report
fullscreen. The request is asynchronous: the window manager or compositor answers with a new size, which shows up as a change
of [[os::Window::ResizeSerial]] after `PumpEvents`, and the swapchain follows it like any resize. A compositor may refuse; then
nothing changes and `IsFullscreen` stays false, so a settings layer treats the `fullscreen` setting as a request, not a promise.

- **Wayland:** `xdg_toplevel_set_fullscreen` and `xdg_toplevel_unset_fullscreen`; `IsFullscreen` reflects the fullscreen
  state in the latest toplevel configure. The request goes out with the next `PumpEvents`.
- **X11:** the `_NET_WM_STATE` client message with `_NET_WM_STATE_FULLSCREEN` sent to the root window (EWMH); `IsFullscreen`
  reads the window's `_NET_WM_STATE` property. A bare X server with no window manager never honors it.
- **Windows and macOS** are not implemented yet; the checklists are in `engine/platform/windows/FULLSCREEN.md` and
  `engine/foundation/os/macos/FULLSCREEN.md`. A backend that does not implement it keeps the default (`SupportsFullscreen` is
  false) and the setting is unavailable.

### Wayland specifics

_TODO_

### X11 specifics

_TODO_

### Known limitations

_TODO_

