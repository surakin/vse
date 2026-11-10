# Tutorial: Handle Keyboard and Mouse Input

**Goal:** Read keys, text, modifiers, the mouse and the wheel every frame, and tell "held" from "just pressed".

**You need:** [Open a Window](hello-window.md). Background: [Platform: Windowing and Input](../guide/platform-windowing-and-input.md).

## Steps

1. **Ask `InputState` what is down now.** After `PumpEvents`, `input.keyboard.IsDown(os::Key::W)` says whether W is held, `input.mouse_x` and `mouse_y` give the
   pointer in window pixels, `input.IsMouseDown(os::kMouseLeft)` a button, and `input.scroll_dy` the wheel since `BeginFrame()`.
2. **Tell pressed from held from released.** `InputState` is a snapshot, so the edge is the difference between this frame's snapshot and the last one:
   *pressed* is down now and up before, *released* is up now and down before. Keep a copy of the previous `KeyboardState` and compare.
3. **Read modifiers and the layout-aware key.** `input.keyboard.modifiers` is a mask (`os::kModShift`, `kModCtrl`, `kModAlt`, `kModSuper`). A `Key` names the key
   that types that letter on the user's layout, so test for commands with keys (W to move) and never use them to build text.
4. **Read text from the event stream.** After `PumpEvents`, loop `while (window->PollEvent(&event))`. A `Text` event carries one character as UTF-8 and handles dead keys
   and AltGr for you. `KeyDown` events also catch a press that began and ended between two frames.
5. **Check it by eye.** The demo `hello_window` logs a line whenever the input changes; the program `input_probe` prints events as they arrive.

## Complete code

The edge detection is plain data work, so this part runs without a window:

```cpp compile run standalone
#include "os/console.h"
#include "platform/input.h"

using namespace engine_placeholder;

// Compares two snapshots of the keyboard for one key.
static const char* Describe(const os::KeyboardState& before, const os::KeyboardState& now, os::Key key) {
    const bool was = before.IsDown(key);
    const bool is = now.IsDown(key);
    if (!was && is) return "pressed";
    if (was && is) return "held";
    if (was && !is) return "released";
    return "up";
}

int main() {
    os::KeyboardState previous;
    os::KeyboardState current;

    // Frame 1: nothing. Frame 2: W goes down. Frame 3: still down. Frame 4: it goes up.
    const bool w_down[4] = {false, true, true, false};
    for (int frame = 0; frame < 4; ++frame) {
        previous = current;                         // copy last frame's snapshot, then let the window update the new one
        current.SetDown(os::Key::W, w_down[frame]);
        os::Print("frame %d: W is %s\n", frame + 1, Describe(previous, current, os::Key::W));
    }
    return 0;
}
```

```output
frame 1: W is up
frame 2: W is pressed
frame 3: W is held
frame 4: W is released
```

In a program with a window, the same comparison sits inside the frame loop. This part needs a display, so it is only compiled:

```cpp compile standalone
#include "os/console.h"
#include "os/os.h"
#include "platform/input.h"
#include "platform/window.h"
#include "platform/window_host.h"

using namespace engine_placeholder;

int main() {
    os::InputState input;
    os::PlatformWindowHost host;
    if (!host.OpenFromEnvironment("Input", 800, 600, &input)) {
        return 1;
    }
    os::Window* window = host.Get();
    os::KeyboardState previous;

    while (!window->ShouldClose()) {
        previous = input.keyboard;                       // last frame's keys
        input.BeginFrame();                              // clears the wheel
        window->PumpEvents();

        if (input.keyboard.IsDown(os::Key::W) && !previous.IsDown(os::Key::W)) {
            os::Print("W pressed\n");
        }
        if ((input.keyboard.modifiers & os::kModCtrl) != 0 && input.keyboard.IsDown(os::Key::S)) {
            os::Print("Ctrl+S held\n");
        }
        if (input.IsMouseDown(os::kMouseLeft)) {
            os::Print("left button at %.0f, %.0f\n", input.mouse_x, input.mouse_y);
        }
        if (input.scroll_dy != 0.0) {
            os::Print("wheel %.1f\n", input.scroll_dy);
        }

        os::InputEvent event;
        while (window->PollEvent(&event)) {              // the ordered stream: text and presses that InputState cannot see
            if (event.type == os::InputEventType::Text) {
                os::Print("typed: %.*s\n", static_cast<int>(event.text_length), event.text);
            }
        }
        if (input.keyboard.IsDown(os::Key::Escape)) {
            break;
        }
        os::SleepNanos(16'000'000);
    }
    return 0;
}
```

## Expected output

Run the second program, click in the window, press W, hold Ctrl and S, type a letter and scroll. The terminal shows lines like:

```text
W pressed
Ctrl+S held
left button at 412, 300
wheel 10.0
typed: a
```

(`Ctrl+S held` repeats every frame while the keys are down, and the left-button line while the button is held; that is "held", not "pressed".)

## Where next

- [Platform: Windowing and Input](../guide/platform-windowing-and-input.md): the event stream in full, the cursor and pointer lock for a camera, the clipboard and drag and drop.
- [UI](../guide/ui.md): the toolkit that turns these events into buttons and text fields.
