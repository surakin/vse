# Platform: Windowing and Input

> **Status:** skeleton — not yet written  
> **Audience:** engine and game programmers  
> **Covers:** Phase 1


## Where the code lives, and what to link

Windows and input are the **platform layer** (layer 9): `engine/platform/`, the CMake library `platform` (`platform_asan`, `platform_tsan`), headers included as
`platform/window.h`, `platform/input.h` and `platform/window_host.h`. The types keep the `os::` namespace. `platform` is the only library that links the display libraries
(Wayland, xkbcommon, X11 on Linux; user32 and shell32 on Windows); a program or test that opens a window links `platform` (the RHI libraries do, so anything on the RHI gets it). A program includes only the neutral
`platform/window_host.h`: `os::PlatformWindowHost` picks and owns the backend, which is defined in `platform/linux/` or `platform/windows/` and never included by a program (the `module_deps_check` gate fails on it).

## Source of truth

- `engine/platform/window.h`
- `input.h`
- `window_host.h` (the neutral host), `linux/window_host_linux.cpp` and `windows/window_host_windows.cpp`
- `entry_point.h` (the windowed programs' entry point) and `linux/entry_point_linux.cpp`, `windows/entry_point_windows.cpp`
- `linux/wayland/*`
- `linux/x11/*`
- `linux/xkb_keyboard.h`
- `windows/win32_window.h`, `windows/win32_keys.h` and `windows/FULLSCREEN.md`
- `linux/key_translate.h` (the keysym translation both Linux backends share)

## API reference

Full reference: [Platform: Windowing and Input](../reference/platform-window-input.md).

- [[os::Window]] — the window interface
- [[os::WindowBackend]] — backend selection (Wayland and X11 on Linux, Win32 on Windows)
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
- **Windows:** borderless over the window's monitor (`engine/platform/windows/FULLSCREEN.md`): the style loses
  `WS_OVERLAPPEDWINDOW` and the window covers the monitor's rectangle; leaving gives back the saved style and placement. Windows
  never refuses, and the change is synchronous: `IsFullscreen` is true when `SetFullscreen` returns. No exclusive mode.
- **macOS** is not implemented yet; the checklist is in `engine/platform/macos/FULLSCREEN.md`. A backend that does not
  implement it keeps the default (`SupportsFullscreen` is false) and the setting is unavailable.

### Wayland specifics

_TODO_

### X11 specifics

_TODO_

### Windows specifics

`os::win32::Win32Window` (`platform/windows/win32_window.h`, Phase 13 Day 158) is a top-level window of a class registered once per
process and never unregistered. The window procedure reaches the object through the window's user data, which `WM_NCCREATE` sets and
closing clears before `DestroyWindow`, so a message sent during creation or destruction, or one still queued for a destroyed window,
never reaches a destroyed object; for the same reason the object cannot move.

- **The size asked for is logical; `Width` and `Height` are physical pixels.** Every engine program is per-monitor-v2 DPI aware (its
  manifest; a thread that is not gets the awareness for the window's creation), so Windows does not scale the window for it. `Open`
  therefore multiplies the size by the window's DPI over 96 ([[os::win32::Win32Window::ScaleForDpi]]): 1280 x 720 asked for is 2560 x 1440
  pixels at 200%, as large to the eye as at 100%, and the swapchain gets every pixel. On `WM_DPICHANGED` (a move to a monitor of another
  scale) the window takes the system's suggested rectangle, so it keeps its size to the eye and its pixel size changes like any resize.
  `Win32Window::Dpi` gives the DPI; [[os::Window]] has no scale factor yet (the UI phase adds one for every backend). On Linux the size
  asked for is used as it is (Wayland's compositor scales the window on a high-density output).
- **Minimized** is `SIZE_MINIMIZED`; `Width` and `Height` keep the size from before, and leaving the minimized state changes
  `ResizeSerial` even when the size did not change.
- **Close:** `WM_CLOSE` (the close button, Alt+F4, the taskbar) sets `ShouldClose`; the window stays until its owner closes it.
  `WM_QUIT` counts as a close request too.
- **Pumping:** `PumpEvents` drains every message of the thread, so with two windows on one thread either one's pump serves both.
- **The modal loops.** While the user drags the frame (or the system menu is open), Windows runs a message loop of its own inside the
  window procedure, and `PumpEvents` does not return until the drag ends. [[os::Window::SetModalLoopCallback]] gives a function that
  is called about every 10 ms during such a loop (a timer the window starts on `WM_ENTERSIZEMOVE` and `WM_ENTERMENULOOP`); a program
  renders a frame from it, reading the size as usual. The callback must not close the window; a `PumpEvents` from inside it returns at
  once. On Linux there is no such loop and the callback is never called.
- **Native handles:** kind `Win32`, `display` the `HINSTANCE` of the module that registered the class, `surface` the `HWND`.

### Windows input

`Win32Window` fills the [[os::InputState]] it was opened with from the window's messages, with the meaning the Linux backends give it.

- **Keys are layout-aware**, as on Linux. A key that types a character is named by the character the active layout gives it with the
  level modifiers held (Shift, Caps Lock, AltGr; not Ctrl or Alt, so Ctrl+A is `Key::A`): on AZERTY the key in the Q position is `A` and
  the `&` key is `Num1` only with Shift, exactly as XKB names them. Every other key is named by its virtual key and scan code: left and
  right Shift by the scan code, Ctrl and Alt by the extended flag, the number pad's navigation keys (NumLock off) are `Unknown` like XKB's
  `KP_Home`, its digits are `Num0`..`Num9`, its Enter is `Enter`, its operators and decimal point are `Unknown`. The translation is
  `os::win32::KeyFromWin32` (`platform/windows/win32_keys.h`), a table test covers every `Key`.
- **A release clears the Key its key was pressed as** (tracked per physical key), as on X11, so changing Shift or NumLock while a key is
  held leaves nothing stuck. Auto-repeat changes nothing. With both Shift keys held, Windows sends one release for both, and it ends both.
- **AltGr** arrives from Windows as a left Ctrl and a right Alt at the same moment; the Ctrl is dropped, so AltGr is `RightAlt` held and
  neither `kModCtrl` nor `kModAlt`, as XKB's level-3 shift. Modifiers come from the keys the window saw held; Caps Lock and NumLock are the
  system's toggles.
- **Alt and F10 alone open no menu** (the window has no menu bar, and menu mode would take the next keys); Alt+Space still opens the system
  menu, Alt+F4 still asks to close, and Alt+letter does not beep.
- **The mouse:** the position is in client coordinates, physical pixels; `pointer_inside` follows the cursor (`WM_MOUSELEAVE`). A press
  captures the mouse, so the release arrives even outside the window (where the position goes on, outside the client area), as X11 and
  Wayland deliver it; held buttons stay held when the pointer leaves. The five buttons are left, right, middle and X1/X2 as back and
  forward. The wheel is 10 units per notch (Linux's scale), down and right positive, and a high-resolution wheel's fractions add up.
- **Focus loss** (`WM_KILLFOCUS`: Alt+Tab, a click elsewhere, another window of the program) releases every key, button and modifier.
  Windows sends it only when the keyboard focus really leaves, not for the window's own menus, the line the X11 backend draws with its grab
  modes. Unlike Linux it also releases the buttons: the other window gets the mouse and the release.
- **Not yet, on any backend:** text input (the characters typed, dead keys and input methods; the UI phase adds it), relative mouse motion
  and cursor capture or hiding: `os::Window` has no call for them.

### Windowed programs: the entry point

A program that opens a window defines `int EngineMain(int argc, char** argv)` and writes `ENGINE_WINDOWED_PROGRAM(EngineMain)` once,
at global scope, instead of `main` (`platform/entry_point.h`). On Linux that is `main`; on Windows it is `wWinMain` with a linker
directive for the GUI subsystem, so starting the program from Explorer opens no console window. `EngineMain` gets UTF-8 arguments
split by the Windows rules for quotes and backslashes, already stashed for `os::CommandLine::FromNative`. Tests and command-line tools
keep a plain `main` and stay console programs; no build file chooses a subsystem. The `platform_neutral_check` gate fails on a windowed
program under `game/` that defines its own `main` (the demos are listed until they reach Windows on Day 161).

A GUI program's output goes to the handles it inherited (a pipe, a redirected file, ctest's capture). Started from a terminal with
none, it attaches to that terminal's console and prints there, after the shell's next prompt (a shell does not wait for a GUI
program). Started from Explorer it has no console: the writes fail and the log file has everything.

### Known limitations

_TODO_

