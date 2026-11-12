# Platform: Windowing and Input

Status: written (Phases 1, 13 and 15).

**Audience:** every programmer who opens a window. Read [Platform: operating system](platform-os.md) first for the console, the clock and the command line.

A window is where a program meets the user: it shows what you draw and delivers what the user does. This page teaches how to open one, run the frame loop,
read the keyboard and mouse, and hand the window to the graphics layer. The engine has its own window code for each system (Wayland, X11, Win32); your
program talks to one interface, [[os::Window]], and never names a backend.

## At a glance

| I want to | Use |
|---|---|
| open a window | `os::PlatformWindowHost::Open` or `OpenFromEnvironment`, then `Get()` |
| let the system deliver events | `window->PumpEvents()` once per frame |
| know the user wants to quit | `window->ShouldClose()` |
| know "is W held right now" | `os::InputState` (`keyboard.IsDown(os::Key::W)`) |
| know "was W pressed since last frame", text, repeats | `window->PollEvent(&event)` after `PumpEvents` |
| draw into the window | `GetNativeInfo()`, `IsMinimized()`, `ResizeSerial()` (see below) |


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

### The Window interface and lifecycle

Two objects are involved. An [[os::InputState]] is plain data that the window fills with "what is down right now". An [[os::PlatformWindowHost]] picks the
backend, opens the connection to the display and owns the window. `host.Get()` returns an `os::Window*`, the interface everything else uses.

    os::InputState input;                                   // declared first: the host borrows it, so it must outlive the host
    os::PlatformWindowHost host;
    if (!host.OpenFromEnvironment("My game", 1280, 720, &input)) {
        return 1;                                           // no backend opened; the reason was logged
    }
    os::Window* window = host.Get();
    // ... the frame loop ...
    // the host's destructor closes the window and the display connection

- **The size is a request.** On Wayland the compositor may choose another size; on Windows the logical size is multiplied by the monitor's DPI. Always read
  `window->Width()` and `Height()` after opening, and again after every resize; they are physical pixels.
- **`Valid()`** is false for a window that failed to open or whose connection died. Check it once after opening if you opened without the host's return value.
- **Ownership.** The host owns the window and its native surface. A graphics surface made from the window must be destroyed before the host (see the native
  handles section).
- **Thread.** Open the window and call every method on one thread. The calls are not synchronized. Other threads may run jobs; they must not touch the window.
- **The title and size are all you pass.** Everything else (fullscreen, cursor, clipboard) is asked of the window afterwards, with a `Supports...` query where
  a backend may lack it.

### Choosing a backend

On Linux a program can talk to a display server in two ways, and a machine may have both (X11 programs run under Wayland through Xwayland). The engine has a
backend for each. You rarely choose: `WindowBackend::Auto` takes **Wayland if `$WAYLAND_DISPLAY` is set, else X11**. A program that wants to let the user
override it uses `OpenFromEnvironment`, which reads `$ENGINE_WINDOW_BACKEND` (`auto`, `wayland` or `x11`; unset means `auto`). The demos add a flag on top:

| Source | Precedence | Example |
|---|---|---|
| `--backend=x11` flag (the demo parses it with `os::ParseWindowBackend`) | highest | `hello_window --backend=wayland` |
| `ENGINE_WINDOW_BACKEND` | middle | `ENGINE_WINDOW_BACKEND=x11 ./hello_window` |
| automatic | lowest | Wayland, else X11 |

**A forced backend never falls back.** `--backend=wayland` on a machine with no Wayland compositor fails and says so; it does not quietly open an X11 window,
because someone who forced a backend is testing it. Only `Auto` tries the next candidate. On Windows the one backend is Win32, on macOS Cocoa (`auto` or `cocoa`; the other names are
refused with the reason). `window->BackendName()` returns `"wayland"`, `"x11"`, `"win32"` or `"cocoa"` for logs and test names.

### Event pumping and the frame loop

A window does nothing until you ask it to. `PumpEvents()` drains everything the system has queued (key presses, pointer moves, a close request, a resize),
without blocking, updates the `InputState`, and fills the event queue. Call it **once per frame, at the start**:

    while (true) {
        input.BeginFrame();                 // clears the per-frame deltas (scroll) so they do not pile up
        window->PumpEvents();
        if (window->ShouldClose()) {
            break;                          // the user closed the window: leave the loop and clean up
        }

        os::InputEvent event;
        while (window->PollEvent(&event)) { /* react to key presses, text, ... */ }

        if (input.keyboard.IsDown(os::Key::Escape)) { break; }
        if (window->IsMinimized()) { os::SleepNanos(16'000'000); continue; }   // nothing to draw to

        // update the game, record and submit the frame
    }

The demo `hello_window` is this loop with a 60 Hz sleep, a time limit (`--run-seconds`), and a line of log output when the input changes. Three things to know:

- **Pumping is what keeps the window alive.** A program that stops pumping looks frozen: the compositor asks "are you there?" and gets no answer. On Windows
  a window drag runs a message loop of its own inside `PumpEvents`; see "Windows specifics" for the callback that keeps rendering during it.
- **`InputState` is a snapshot of now.** It cannot tell that a key was pressed and released between two frames. If a click must never be lost, read the event
  stream (next sections).
- **Do not call `PumpEvents` from several places** (a nested loop in a dialog, say) unless you mean to: events are consumed by whoever pumps.

### Reading the keyboard and mouse with InputState

`InputState` is what most game code wants: a few cheap questions answered from the latest pump.

    if (input.keyboard.IsDown(os::Key::W)) { position.z -= speed * dt; }        // held now
    const bool shift = (input.keyboard.modifiers & os::kModShift) != 0;         // kModShift, kModCtrl, kModAlt, kModSuper, kModCapsLock, kModNumLock
    const f64 x = input.mouse_x, y = input.mouse_y;                             // window-local, physical pixels; the last position if the pointer left
    if (input.IsMouseDown(os::kMouseLeft)) { /* a button is held */ }
    camera_zoom += input.scroll_dy;                                             // accumulated since BeginFrame(): positive is down/right

- **Keys are layout-aware.** `os::Key::W` is the key that *types* "w" on the user's layout (through libxkbcommon on Linux), so on an AZERTY keyboard the key
  in the Z position is `W`. A key that has no entry in the enum is `Key::Unknown`. Use the keys for commands (move, jump); use the `Text` events for typing.
- **`pointer_inside`** tells whether the pointer is over the window; `mouse_x/y` keep the last position when it is not.
- **Edge detection is yours.** For "pressed this frame", keep the previous frame's `KeyboardState` and compare, as `hello_window` does with its
  `InputSnapshot`; or use the `KeyDown` event, which also catches a press that ended within the frame.

### Reading input (keys, text, mouse, modifiers)

`InputState` answers "what is down now". It cannot say that a key was pressed and released between two polls, it carries no text, and on Wayland it has no
key repeat. A program that needs those (the UI toolkit does) drains the **input event stream** after each `PumpEvents`:

    window.PumpEvents();
    os::InputEvent event;
    while (window.PollEvent(&event)) {
        // event.type: KeyDown (event.repeat for an auto-repeat), KeyUp, Text, PointerMove, PointerButton, Wheel,
        // PointerEnter, PointerLeave, FocusGained, FocusLost, Resize
    }

- **Order.** Events come out in the order the server delivered them. A `Text` event follows the `KeyDown` that produced it, and a repeat sends both again.
- **Text** is one Unicode scalar as UTF-8 (`InputEvent::text`, `text_length`). A dead key writes nothing until the next key completes the sequence (German
  acute then `e` is one `Text` of "é"), AltGr+`e` on a German layout is "€", and a press with Ctrl or Alt held, or Enter, Tab, Backspace, Escape or Delete, writes
  none (those are keys, not text). There is no input-method composition; text is what the layout and the locale's compose table give.
- **Repeat.** X11 turns on detectable auto-repeat, so a held key is one `KeyDown` and then repeats with `repeat` set. Wayland sends no repeats, so the backend
  makes them from the compositor's rate and delay inside `PumpEvents` (at most four per pump after a stall). Only the last pressed key repeats; a modifier never does.
- **Focus.** When the window loses focus every key still held gets a `KeyUp`, then `FocusLost` arrives, so a program following the stream never keeps a key down.
- **Wheel** is in notches, positive right and down, like `InputState::scroll_dx/dy` (which counts ten units to a notch).
- **Overflow.** The queue holds 1,024 events. When the program does not drain it, the newest events are dropped and counted (`Window::DroppedEvents`); the older ones stay in order.
- `InputState` is unchanged and agrees with the stream; a program may use either, or both.

### The mouse cursor and the pointer lock

Three things change what the pointer looks like and does; each has a `Supports...` query because a backend or compositor may lack it:

    window.SetCursor(os::CursorShape::IBeam);          // the standard shapes: Arrow, IBeam, Hand, the resize arrows, ResizeAll, NotAllowed, Hidden
    os::CursorImage image;                             // a custom cursor: premultiplied RGBA8, up to 128x128, hotspot inside, scale 1 to 4
    window.SetCursorImage(image);                      // replaces the shape until ClearCursorImage()
    window.SetPointerLock(true);                       // a camera control: the pointer stops, RelativeMotion events arrive

- **Shapes** are set once and stay while the pointer is over the window (Wayland re-applies them on every enter). `Hidden` shows no cursor.
- **Images** are copied by the window; setting an equal image again costs nothing, a different one frees the old one, and an invalid one (above 128x128, the hotspot outside, no pixels, a bad scale) is refused and changes nothing. On Wayland the image is drawn at `width / scale` logical units; X11 has no cursor scaling and shows the pixels as they are.
- **The lock** holds only while the window has focus: it is released on focus loss and asked for again on focus gain while the program still wants it, and a request made while unfocused waits for focus. While it holds, `PollEvent` returns `RelativeMotion` events (`x`, `y` are the device deltas, unaccelerated where the system says so) and never otherwise. Pair it with `SetCursor(Hidden)` for a camera. Destroying the window releases it.
- Wayland needs `wp_cursor_shape_v1` for the shapes and `zwp_pointer_constraints_v1` with `zwp_relative_pointer_v1` for the lock; X11 needs nothing special (the lock is a pointer grab with XInput 2 raw motion). The `input_probe` program (`L` lock, `C` shapes, `I` image) shows all of it by eye.

### The clipboard

The system clipboard and, on Linux, the primary (middle-click) selection:

    os::ClipboardOffer offer;                                           // what to put there: a format and bytes (copied by the window)
    offer.data = reinterpret_cast<const byte*>(text); offer.size = length;
    window.SetClipboard(os::ClipboardKind::Clipboard, offer);           // Text by default; FileList and Custom (with custom_name) too
    const u32 id = window.RequestClipboard(os::ClipboardKind::Clipboard, os::ClipboardFormat::Text, StringView());
    // later, when PollEvent returns a ClipboardData event with request_id == id:
    os::ClipboardResult result;
    if (window.TakeClipboardResult(id, &result) && result.status == os::ClipboardStatus::Ok) { /* result.bytes is valid UTF-8 */ }

- **Asking is asynchronous**: another program must answer, so `RequestClipboard` returns an id (0 if it cannot start) and the answer is a `ClipboardData` event; `TakeClipboardResult` hands the result over once. A request ends exactly once, with `Ok`, `Missing` (empty, or the owner lacks the format), `Timeout` (no progress for 2 s: the owner is gone or stuck), `TooLarge` (above 16 MiB) or `Failed`. At most eight requests are open at once and eight unread results are kept (the oldest is dropped).
- **`ClipboardHasFormat`** says whether the selection offers a format, for a Paste command that should be greyed out. **`ClipboardChanged`** arrives when another program takes the selection.
- **Text is checked**: invalid UTF-8 from another program is replaced with U+FFFD and `replaced_sequences` counts them. **File lists are checked**: only `file://` URIs (no host or `localhost`) become absolute local paths, percent-decoded; `..`, relative paths, other hosts, `http:` and control bytes are dropped and counted in `rejected_entries`. No network location is ever followed.
- **Wayland** accepts a selection only with the serial of a recent key or button press in your window, so `SetClipboard` fails until the user has pressed a key or button there; call it from the handler of the copy command. **X11** has no such rule. Both keep the data in the window, so the selection ends with the window (no clipboard manager keeps it).
- The `input_probe` keys `Y` (copy a test string), `P` (paste), `O` (paste primary) and `F` (formats) check it against other programs by eye.

### Drag and drop between programs

Dropping onto your window and dragging out of it (Wayland, X11):

    os::DropAccept accept;
    accept.formats = os::kDropText | os::kDropFiles;                    // nothing accepted means the window refuses every drag
    window.SetDropAccept(accept);
    // PollEvent: DragEnter / DragOver carry x, y, drag_formats and drag_actions; answer with
    window.SetDropAction(os::DragAction::Copy);                         // Nothing refuses at this position
    // on Drop: read the data, then finish
    const u32 id = window.RequestDropData(os::ClipboardFormat::FileList, StringView());
    // when the DropData event arrives: window.TakeClipboardResult(id, &result), then
    window.FinishDrop(os::DragAction::Copy);

    os::DragSource source;                                              // dragging out: the bytes are copied now
    source.offers = &offer; source.offer_count = 1; source.allowed_actions = os::kAllDragActions;
    window.BeginDrag(source);                                           // later: a DragEnded event with the action taken

- **Same rules as the clipboard**: dropped text is checked UTF-8, dropped files become absolute local paths through the same `file://` rules, and a list is cut at 10,000 entries with the rest counted in `rejected_entries`. A source offering more than 64 types is read only up to the 64th.
- **A drag no format of which is accepted** gets a refusing answer and no events. A drop the program never finishes is finished for it after 5 s; a target that never answers a drag we started cancels it after 1 s (`DragEnded` with `Nothing`).
- **Actions**: Shift asks for move and Ctrl for copy where the platform reports modifiers; Escape cancels a drag we started. Wayland has copy and move only.
- **X11** does not draw the drag image (`DragSource::image` is ignored); **Wayland** shows it as the drag icon. Lazy data is not supported: the bytes are copied at `BeginDrag`.
- The `input_probe` key `D` starts a drag of a test string; drops on its window are printed.

### Resize, close, focus

Three things the user does to a window that a program has to survive:

- **Resize.** After `PumpEvents`, `Width()` and `Height()` hold the new size, `ResizeSerial()` has changed, and a `Resize` event is in the queue. A program
  that draws remembers the serial it built its swapchain for and rebuilds when it differs (the next section). Do not rebuild on every `Resize` event
  by hand if you already compare the serial: dragging a frame can send dozens a second, and the serial check does the work once per frame.
- **Close.** The close button, Alt+F4 and the compositor's close all do the same: `ShouldClose()` turns true. **The window is not destroyed**, because a
  program may want to ask "save changes?" first. There is no call to cancel the request, so a program that wants a "save changes?" question shows it and
  keeps its own "really quit" state instead of testing `ShouldClose()` alone. To quit, leave the loop and let the host go out of scope.
- **Focus.** Losing focus releases every held key (and, on Windows, button), so a key never stays "down" because the user switched windows. `FocusGained`
  and `FocusLost` events tell a game to pause or to release its pointer lock. Keep rendering or not is your policy; `IsMinimized()` is the one reliable
  "do not draw" signal.

A resize or a minimize on one window changes only that window's serial; with two windows, keep one serial per swapchain.

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

Code: `engine/platform/linux/wayland/`. Wayland is the primary Linux backend; it is built on the reference client library `libwayland-client` and
`libxkbcommon` for keymaps. What differs from what you may expect:

- **The compositor is in charge.** The window is an `xdg_toplevel` (xdg-shell). The size you ask for is a hint; the compositor answers with a *configure*
  and the window takes that size. Server-side decorations are requested through `xdg-decoration` (the compositor draws the title bar); a compositor that
  refuses leaves the window undecorated.
- **A resize commits a new buffer.** The window attaches a buffer of the new size after each configure; the graphics layer's swapchain follows through
  `ResizeSerial()` like on any backend.
- **No minimized or hidden state is told to a client.** `IsMinimized()` is true only for a zero-area window, so do not rely on it to save power while
  the window is hidden.
- **`ContentScale()` is 1.** The compositor scales the window itself on a high-density output.
- **No key repeat from the server.** The backend creates repeats from the compositor's rate and delay inside `PumpEvents`.
- **The clipboard needs a recent user action** (a key or button serial from your window); the pointer lock and cursor shapes need compositor protocols
  (`zwp_pointer_constraints_v1`, `wp_cursor_shape_v1`) that most but not all compositors offer: ask `SupportsPointerLock()` and `SupportsCursorShape()`.
- **A lost connection ends the program politely.** If the compositor goes away, `ShouldClose()` turns true instead of the program hanging.
- **Requests go out on your calls.** All of a window's requests are made on `PumpEvents` and when you call a setter; a program that never pumps never gets its
  fullscreen or clipboard request out.
- **Testing.** The Wayland half is checked against a real compositor only (see Known limitations).

### X11 specifics

Code: `engine/platform/linux/x11/`. X11 is the fallback for machines without a Wayland session, built on `libX11` with `libXcursor`, `libXi` and `libXfixes`.

- **A window manager does the decoration and the close.** The close button is the `WM_DELETE_WINDOW` protocol message, which sets `ShouldClose()`. The title
  is written in both `WM_NAME` and `_NET_WM_NAME` (UTF-8). Without a window manager (a bare X server) there are no decorations and fullscreen is never honored.
- **Detectable auto-repeat is turned on**, so a held key is one `KeyDown` followed by repeats flagged `repeat`, instead of the X11 default of release and press pairs.
- **Minimized is visible** here: a window the window manager unmapped or iconified (also on a hidden desktop) makes `IsMinimized()` true.
- **`ContentScale()` is the desktop's `Xft.dpi` over 96** (1 to 4), read from the root window's `RESOURCE_MANAGER` when the window is created and again whenever the desktop rewrites it (no event is queued and the window keeps its size: read `ContentScale()` every frame, as the UI adapter does; the last `Xft.dpi` line wins, as in any X resource database). X11 has
  no per-window or per-monitor scale, and the server's own dpi (`xdpyinfo`) is usually 96 whatever the desktop is set to, so the resource is the signal GTK, Qt and Chromium use too. Under Xwayland
  with a scaled output the compositor leaves X11 clients unscaled and sets `Xft.dpi` instead. `PlatformWindowHost::Open` takes its size in logical pixels and multiplies it by this scale, as on
  Windows; `X11Window::Create` takes pixels unless it is given `SizeUnits::Logical`.
- **Pointer lock is a grab** with XInput 2 raw motion for the deltas. Pointer lock and the clipboard behave as on Wayland, minus the "recent click" rule.
- **Drag images are not drawn**; the data transfer works.
- **Under Wayland through Xwayland** an X11 window works, so `--backend=x11` is a way to test this backend on a Wayland desktop.

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

### macOS specifics

`os::cocoa::CocoaWindow` (`platform/macos/cocoa_window.h`, Phase 26 Day 311) is an `NSWindow` whose content view is backed by a
`CAMetalLayer`, driven by the program's own loop: `PumpEvents` drains AppKit's event queue without waiting, for every window of the program.

- **The main thread only.** AppKit allows windows on the main thread alone; `Open` on another thread fails with a log line (AppKit would
  abort the process). Keep the window, and the loop that pumps it, on the thread that runs `main`.
- **The application** (`NSApplication`, a regular app with a Dock icon and a menu with Quit) is set up once, by the windowed program's
  entry point or by the first window a test opens. No app bundle and no `Info.plist` are needed: a program run from a terminal becomes the
  active app with a key window (about 0.1 s after the window appears, measured), gets Retina pixels, and its frames pace to the display.
- **The size asked for is in points; `Width` and `Height` are pixels** (points times the backing scale, 2 on a Retina display), as on
  Windows. `ContentScale` is the backing scale; a move to a display of another scale changes the pixel size and `ResizeSerial`, and the
  layer's `contentsScale` follows.
- **Close and Quit are requests.** The close button sets `ShouldClose`; the window stays until its owner closes it. Quit (Cmd-Q, the
  menu, the Dock) does not end the process: it makes every window's `ShouldClose` true (`os::cocoa::QuitRequested`), and a program that
  decides not to quit takes it back with `os::cocoa::ClearQuitRequest`. A logout, restart or shutdown asks the same way, but the system
  is told to wait (`NSTerminateLater`) instead of refused: it goes on when the program ends, and is cancelled by `ClearQuitRequest`.
- **Live resize** runs AppKit's own loop, as Windows' modal loop does: `SetModalLoopCallback`'s function is called from each resize step.
- **Fullscreen** is native fullscreen (`toggleFullScreen:`, a Space of its own): `SetFullscreen` starts the animation and `IsFullscreen`
  follows when it ends; the new size arrives as a resize (`platform/macos/FULLSCREEN.md`).
- **Without a window server** (an ssh login with nobody at the console) no window opens, and the log says why; a locked screen still
  opens windows, but nobody sees them, so the window tests skip while it is locked.
- **Native handles:** kind `Cocoa`, `display` the content `NSView*`, `surface` its `CAMetalLayer*`, `window_id` the window number.
- **Input** (keys, text, the mouse) comes on Phase 26 Day 312; today the stream has `Resize`, `FocusGained` and `FocusLost`.

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
keep a plain `main` and stay console programs; no build file chooses a subsystem. On macOS it is `main`, which sets up the application
object (a regular app with a Quit menu) before `EngineMain` runs. The `platform_neutral_check` gate fails on a windowed
program under `game/` that defines its own `main` (the demos are listed until they reach Windows on Day 161).

A GUI program's output goes to the handles it inherited (a pipe, a redirected file, ctest's capture). Started from a terminal with
none, it attaches to that terminal's console and prints there, after the shell's next prompt (a shell does not wait for a GUI
program). Started from Explorer it has no console: the writes fail and the log file has everything.

### Known limitations

- The event stream is implemented on Linux (Wayland and X11); the Windows backend gets it on Phase 15 Day 205, macOS in Phase 26 (`engine/platform/BACKEND.md`).
- The cursor shapes, images and the lock are implemented on Linux; Windows gets them on Phase 15 Day 205, macOS in Phase 26 (`engine/platform/BACKEND.md`).
- No input-method (IME) composition: a Chinese, Japanese or Korean input method is not supported; dead keys and compose sequences are.
- The Wayland half is checked against a real compositor only, which cannot be made to send key, pointer and focus events; the key logic is tested through
  `KeyboardEvents` with a fake clock and real keymaps, and the pointer, focus and wheel handlers by the end-to-end runs of the UI demo (Day 204).

