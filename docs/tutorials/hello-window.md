# Tutorial: Open a Window

**Goal:** Open a window on Wayland or X11, run a frame loop, and close it cleanly when the user asks.

**You need:** a built tree (see [Getting started](../guide/getting-started.md)) and a desktop session. Background: [Platform: Windowing and Input](../guide/platform-windowing-and-input.md).

## Steps

1. **Run the demo first.** `./build/game/hello_window/hello_window` opens a 1280x720 window for six seconds and exits. Closing it earlier ends the program at once. If nothing
   appears, you are probably on a machine without a display; the program logs why.
2. **Pick a backend.** `hello_window --backend=x11` or `--backend=wayland` forces one; without the flag the program reads `ENGINE_WINDOW_BACKEND`, and without
   that it takes Wayland when `$WAYLAND_DISPLAY` is set and X11 otherwise. A forced backend never falls back: if it cannot open, the program says so and stops.
3. **Read the frame loop.** Every frame does the same four things: clear the per-frame input deltas (`input.BeginFrame()`), let the system deliver events
   (`window->PumpEvents()`), check whether the user wants to leave (`window->ShouldClose()`), then update and draw. The demo ends the frame by sleeping to
   reach 60 Hz; a renderer would wait for the swapchain instead.
4. **Handle the close request.** `ShouldClose()` turns true when the user clicks the close button. The window is *not* destroyed for you: leave the loop and
   let the `PlatformWindowHost` go out of scope, which closes the window and the display connection.
5. **Try the flags.** `--run-seconds=30` keeps the window longer, `--frame-times` prints the time of each frame, `--log-level=debug` shows what the backend does.

## Complete code

A window program in miniature. It runs for at most five seconds, so you can start it from a terminal and just close the window.

```cpp compile standalone
#include "os/console.h"
#include "os/os.h"
#include "platform/input.h"
#include "platform/window.h"
#include "platform/window_host.h"

using namespace vse;

int main() {
    os::InputState input;                          // declared before the host: the host borrows it
    os::PlatformWindowHost host;
    if (!host.OpenFromEnvironment("Hello window", 1280, 720, &input)) {
        os::PrintError("no window could be opened\n");
        return 1;
    }
    os::Window* window = host.Get();
    os::Print("opened a %ux%u window on %s\n", window->Width(), window->Height(), window->BackendName());

    const u64 start = os::MonotonicTimeNanos();
    while (os::MonotonicTimeNanos() - start < 5'000'000'000ull) {
        input.BeginFrame();
        window->PumpEvents();
        if (window->ShouldClose()) {
            os::Print("close requested\n");
            break;
        }
        if (input.keyboard.IsDown(os::Key::Escape)) {
            break;                                 // Escape quits too
        }
        os::SleepNanos(16'000'000);                // about 60 frames a second
    }
    return 0;                                      // the host closes the window
}
```

## Expected output

On a Wayland desktop (the size may differ with your compositor):

```text
opened a 1280x720 window on wayland
close requested
```

The second line appears only if you close the window within five seconds; otherwise the program simply ends.

## Where next

- [Handle Keyboard and Mouse Input](handling-input.md): react to what the user does in the window.
- [Clear a Window with the RHI](hello-rhi.md): put pixels in it.
- [Platform: Windowing and Input](../guide/platform-windowing-and-input.md): resize, fullscreen, cursor, clipboard.
