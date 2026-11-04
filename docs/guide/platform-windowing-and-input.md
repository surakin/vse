# Platform: Windowing and Input

> **Status:** skeleton — not yet written  
> **Audience:** engine and game programmers  
> **Covers:** Phase 1

## Source of truth

- `engine/foundation/os/window.h`
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

#include "os/window.h"

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

### Wayland specifics

_TODO_

### X11 specifics

_TODO_

### Known limitations

_TODO_

