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

### Wayland specifics

_TODO_

### X11 specifics

_TODO_

### Known limitations

_TODO_

